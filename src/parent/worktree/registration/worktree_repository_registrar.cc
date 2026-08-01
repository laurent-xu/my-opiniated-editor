#include "src/parent/worktree/registration/worktree_repository_registrar.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "src/base/ascii_whitespace.h"
#include "src/parent/worktree/git_worktree_list.h"
#include "src/parent/worktree/registration/repository_registration_request.h"
#include "src/parent/worktree/registration/repository_root_state.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"
#include "src/process/command_runner.h"

namespace moe::parent {
namespace {

constexpr std::string_view GIT_POINTER_CONTENT = "gitdir: ./.bare";
constexpr std::string_view REMOTE_FETCH_REFSPEC = "+refs/heads/*:refs/remotes/origin/*";

void require_success(process::CommandResult const& result, std::string const& action) {
  if (!result.exit_status.succeeded()) {
    throw std::runtime_error(action + " failed with exit code " +
                             std::to_string(result.exit_status.value()));
  }
}

std::filesystem::path normalized_absolute_path(std::filesystem::path const& path,
                                               std::string const& description) {
  if (path.empty() || !path.is_absolute()) {
    throw std::invalid_argument(description + " must be an absolute path");
  }
  std::error_code error;
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
  if (error != std::error_code{}) {
    throw std::invalid_argument("failed to normalize " + description + ": " + path.string());
  }
  while (normalized != normalized.root_path() && normalized.filename().empty()) {
    normalized = normalized.parent_path();
  }
  return normalized;
}

bool directory_is_empty(std::filesystem::path const& path) {
  std::error_code error;
  bool const empty = std::filesystem::is_empty(path, error);
  if (error != std::error_code{}) {
    throw std::filesystem::filesystem_error("inspect repository root", path, error);
  }
  return empty;
}

std::string read_git_pointer(std::filesystem::path const& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to read repository .git pointer: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), {}};
}

void write_git_pointer(std::filesystem::path const& root) {
  std::filesystem::path const pointer_path = root / ".git";
  std::ofstream output(pointer_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("failed to write repository .git pointer: " + pointer_path.string());
  }
  output << GIT_POINTER_CONTENT << '\n';
  output.flush();
  if (!output) {
    throw std::runtime_error("failed to write repository .git pointer: " + pointer_path.string());
  }
}

void merge_repository(persistence::WorktreeRegistry& registry,
                      std::filesystem::path const& repository_root,
                      std::vector<std::filesystem::path> const& discovered_worktrees) {
  persistence::Repository* destination = nullptr;
  for (persistence::Repository& repository : *registry.mutable_repositories()) {
    if (repository.root_path() == repository_root.string()) {
      destination = &repository;
      break;
    }
  }
  if (destination == nullptr) {
    destination = registry.add_repositories();
    destination->set_root_path(repository_root.string());
  }

  std::set<std::string> worktree_paths;
  for (persistence::Worktree const& worktree : destination->worktrees()) {
    worktree_paths.insert(worktree.path());
  }
  for (std::filesystem::path const& worktree : discovered_worktrees) {
    worktree_paths.insert(worktree.string());
  }

  destination->clear_worktrees();
  for (std::string const& worktree_path : worktree_paths) {
    destination->add_worktrees()->set_path(worktree_path);
  }
}

}  // namespace

RepositoryRootState inspect_repository_root(std::filesystem::path const& repository_root) {
  std::filesystem::path const root = normalized_absolute_path(repository_root, "repository root");
  std::error_code error;
  bool const exists = std::filesystem::exists(root, error);
  if (error != std::error_code{}) {
    throw std::filesystem::filesystem_error("inspect repository root", root, error);
  }
  if (!exists) {
    return RepositoryRootState::EMPTY;
  }
  if (!std::filesystem::is_directory(root, error) || error != std::error_code{}) {
    throw std::invalid_argument("repository root must be a directory: " + root.string());
  }
  if (directory_is_empty(root)) {
    return RepositoryRootState::EMPTY;
  }

  std::filesystem::path const bare_directory = root / ".bare";
  if (!std::filesystem::is_directory(bare_directory, error) || error != std::error_code{}) {
    throw std::invalid_argument("repository root must be empty or contain a .bare directory: " +
                                root.string());
  }

  std::filesystem::path const pointer_path = root / ".git";
  bool const pointer_exists = std::filesystem::exists(pointer_path, error);
  if (error != std::error_code{}) {
    throw std::filesystem::filesystem_error("inspect repository .git pointer", pointer_path, error);
  }
  if (!pointer_exists) {
    return RepositoryRootState::RECOVERABLE_BARE_ROOT;
  }
  if (!std::filesystem::is_regular_file(pointer_path, error) || error != std::error_code{} ||
      base::trim_ascii_whitespace(read_git_pointer(pointer_path)) != GIT_POINTER_CONTENT) {
    throw std::invalid_argument("repository .git must point to ./.bare: " + pointer_path.string());
  }
  return RepositoryRootState::BARE_ROOT;
}

std::string configured_git_executable() {
  char const* executable = std::getenv("MOE_GIT_EXECUTABLE");
  if (executable != nullptr && executable[0] != '\0') {
    return executable;
  }
  return "git";
}

WorktreeRepositoryRegistrar::WorktreeRepositoryRegistrar(std::string executable)
    : git_executable(std::move(executable)) {
  if (git_executable.empty()) {
    throw std::invalid_argument("git executable must not be empty");
  }
}

void WorktreeRepositoryRegistrar::register_repository(RepositoryRegistrationRequest const& request,
                                                      std::ostream& progress) const {
  std::filesystem::path const root =
      normalized_absolute_path(request.repository_root, "repository root");
  WorktreeRegistryStore const store(request.registry_path);
  persistence::WorktreeRegistry registry = store.load();
  RepositoryRootState state = inspect_repository_root(root);
  bool cloned = false;
  bool created_root = false;

  if (state == RepositoryRootState::EMPTY) {
    if (!request.clone_url.has_value() || base::trim_ascii_whitespace(*request.clone_url).empty()) {
      throw std::invalid_argument("clone URL is required for a new repository root");
    }

    std::error_code create_error;
    if (!std::filesystem::exists(root)) {
      created_root = true;
      std::filesystem::create_directories(root, create_error);
      if (create_error != std::error_code{}) {
        throw std::filesystem::filesystem_error("create repository root", root, create_error);
      }
    }

    progress << "Cloning bare repository...\n";
    process::CommandResult const clone = process::run_command(
        {git_executable, "clone", "--bare", base::trim_ascii_whitespace(*request.clone_url),
         (root / ".bare").string()},
        process::StandardOutputMode::INHERIT);
    if (!clone.exit_status.succeeded()) {
      std::error_code cleanup_error;
      std::filesystem::remove_all(root / ".bare", cleanup_error);
      if (created_root) {
        std::filesystem::remove(root, cleanup_error);
      }
      throw std::runtime_error("git clone --bare failed with exit code " +
                               std::to_string(clone.exit_status.value()));
    }
    cloned = true;
    state = RepositoryRootState::RECOVERABLE_BARE_ROOT;
  }

  if (state == RepositoryRootState::RECOVERABLE_BARE_ROOT) {
    write_git_pointer(root);
  }

  std::filesystem::path const bare_directory = root / ".bare";
  process::CommandResult const bare_check = process::run_command(
      {git_executable, "--git-dir", bare_directory.string(), "rev-parse", "--is-bare-repository"},
      process::StandardOutputMode::CAPTURE);
  require_success(bare_check, "validate bare repository");
  if (base::trim_ascii_whitespace(bare_check.standard_output) != "true") {
    throw std::runtime_error("repository .bare directory is not a bare Git repository");
  }

  if (cloned || state == RepositoryRootState::RECOVERABLE_BARE_ROOT) {
    progress << "Configuring remote branches...\n";
    require_success(
        process::run_command({git_executable, "--git-dir", bare_directory.string(), "config",
                              "remote.origin.fetch", std::string(REMOTE_FETCH_REFSPEC)},
                             process::StandardOutputMode::INHERIT),
        "configure remote fetch");
    progress << "Fetching remote branches...\n";
    require_success(process::run_command(
                        {git_executable, "--git-dir", bare_directory.string(), "fetch", "origin"},
                        process::StandardOutputMode::INHERIT),
                    "fetch remote branches");
  }

  process::CommandResult const default_branch = process::run_command(
      {git_executable, "--git-dir", bare_directory.string(), "symbolic-ref", "--quiet", "HEAD"},
      process::StandardOutputMode::CAPTURE);
  require_success(default_branch, "resolve default branch");
  std::string const default_branch_ref =
      base::trim_ascii_whitespace(default_branch.standard_output);
  if (!default_branch_ref.starts_with("refs/heads/")) {
    throw std::runtime_error("repository does not expose a default branch");
  }
  process::CommandResult const default_branch_commit =
      process::run_command({git_executable, "--git-dir", bare_directory.string(), "rev-parse",
                            "--verify", "--quiet", default_branch_ref + "^{commit}"},
                           process::StandardOutputMode::CAPTURE);
  require_success(default_branch_commit, "resolve default branch commit");
  if (base::trim_ascii_whitespace(default_branch_commit.standard_output).empty()) {
    throw std::runtime_error("repository default branch does not resolve to a commit");
  }

  process::CommandResult const worktree_list =
      process::run_command({git_executable, "--git-dir", bare_directory.string(), "worktree",
                            "list", "--porcelain", "-z"},
                           process::StandardOutputMode::CAPTURE);
  require_success(worktree_list, "list repository worktrees");
  merge_repository(registry, root, available_git_worktree_paths(worktree_list.standard_output));

  try {
    store.save(registry);
  } catch (std::exception const& error) {
    if (cloned) {
      throw std::runtime_error("repository was created but registry update failed: " +
                               std::string(error.what()));
    }
    throw;
  }

  progress << "Repository registered: " << root.string() << '\n';
}

}  // namespace moe::parent
