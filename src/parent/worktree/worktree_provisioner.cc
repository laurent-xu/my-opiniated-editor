#include "src/parent/worktree/worktree_provisioner.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "src/parent/worktree/registry/worktree_registry_store.h"
#include "src/parent/worktree/worktree_provision_kind.h"
#include "src/parent/worktree/worktree_provision_request.h"
#include "src/parent/worktree/worktree_provision_result.h"
#include "src/process/command_runner.h"

namespace moe::parent {
namespace {

std::string trimmed(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' ||
                            value.back() == '\t')) {
    value.pop_back();
  }
  std::size_t start = 0;
  while (start < value.size() && (value[start] == '\n' || value[start] == '\r' ||
                                  value[start] == ' ' || value[start] == '\t')) {
    ++start;
  }
  return value.substr(start);
}

std::string read_file(std::filesystem::path const& path, std::string const& description) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::invalid_argument("failed to read " + description + ": " + path.string());
  }
  std::string value{std::istreambuf_iterator<char>(input), {}};
  if (input.bad()) {
    throw std::invalid_argument("failed to read " + description + ": " + path.string());
  }
  return value;
}

std::filesystem::path canonical_path(std::filesystem::path const& path,
                                     std::string const& description) {
  if (path.empty() || !path.is_absolute()) {
    throw std::invalid_argument(description + " must be an absolute path");
  }
  std::error_code error;
  std::filesystem::path const canonical = std::filesystem::weakly_canonical(path, error);
  if (error != std::error_code{}) {
    throw std::invalid_argument("failed to resolve " + description + ": " + path.string());
  }
  return canonical;
}

bool is_strict_descendant(std::filesystem::path const& path,
                          std::filesystem::path const& ancestor) {
  std::filesystem::path const relative = path.lexically_relative(ancestor);
  if (relative.empty() || relative == ".") {
    return false;
  }
  return std::ranges::all_of(
      relative, [](std::filesystem::path const& component) { return component != ".."; });
}

void validate_branch(std::string const& branch) {
  if (branch.empty()) {
    throw std::invalid_argument("Branch must not be empty");
  }
  if (branch == "@" || branch.front() == '/' || branch.back() == '/' ||
      branch.find("//") != std::string::npos || branch.find("..") != std::string::npos ||
      branch.find("@{") != std::string::npos) {
    throw std::invalid_argument("Branch is not a valid Git branch name");
  }

  constexpr std::string_view FORBIDDEN = " ~^:?*[\\";
  std::size_t component_start = 0;
  for (std::size_t index = 0; index <= branch.size(); ++index) {
    if (index < branch.size()) {
      auto const byte = static_cast<unsigned char>(branch[index]);
      if (byte < 0x20U || byte == 0x7FU ||
          FORBIDDEN.find(static_cast<char>(byte)) != std::string_view::npos) {
        throw std::invalid_argument("Branch is not a valid Git branch name");
      }
    }
    if (index == branch.size() || branch[index] == '/') {
      std::string_view const component(branch.data() + component_start, index - component_start);
      if (component.empty() || component.front() == '.' || component.back() == '.' ||
          component.ends_with(".lock")) {
        throw std::invalid_argument("Branch is not a valid Git branch name");
      }
      component_start = index + 1U;
    }
  }
}

std::filesystem::path validated_worktree_path(WorktreeProvisionRequest const& request,
                                              std::filesystem::path const& repository_root) {
  std::filesystem::path const path = canonical_path(request.worktree_path, "worktree path");
  if (!is_strict_descendant(path, repository_root) ||
      is_strict_descendant(path, repository_root / ".bare") || path == repository_root / ".bare") {
    throw std::invalid_argument("Worktree path must be below the repository root");
  }
  return path;
}

void validate_existing_worktree(std::filesystem::path const& worktree_path,
                                std::filesystem::path const& repository_root,
                                std::string const& branch) {
  std::error_code error;
  if (!std::filesystem::is_directory(worktree_path, error) || error != std::error_code{}) {
    throw std::invalid_argument("Existing worktree path must be a directory");
  }

  std::filesystem::path const pointer_path = worktree_path / ".git";
  if (!std::filesystem::is_regular_file(pointer_path, error) || error != std::error_code{}) {
    throw std::invalid_argument("Existing worktree must contain a .git pointer file");
  }
  std::string const pointer = trimmed(read_file(pointer_path, "worktree .git pointer"));
  constexpr std::string_view PREFIX = "gitdir: ";
  if (!pointer.starts_with(PREFIX)) {
    throw std::invalid_argument("Existing worktree has an invalid .git pointer");
  }

  std::filesystem::path administrative_path(pointer.substr(PREFIX.size()));
  if (administrative_path.is_relative()) {
    administrative_path = worktree_path / administrative_path;
  }
  administrative_path = canonical_path(administrative_path, "worktree administrative path");
  std::filesystem::path const administrative_root = canonical_path(
      repository_root / ".bare" / "worktrees", "repository worktree administrative directory");
  if (!is_strict_descendant(administrative_path, administrative_root)) {
    throw std::invalid_argument("Existing worktree does not belong to the selected repository");
  }

  std::string const head = trimmed(read_file(administrative_path / "HEAD", "worktree HEAD"));
  if (head != "ref: refs/heads/" + branch) {
    throw std::invalid_argument("Existing worktree branch does not match the entered branch");
  }
}

persistence::Repository* registered_repository(persistence::WorktreeRegistry& registry,
                                               std::filesystem::path const& root) {
  for (persistence::Repository& repository : *registry.mutable_repositories()) {
    if (repository.root_path() == root.string()) {
      return &repository;
    }
  }
  throw std::invalid_argument("Selected repository is not registered");
}

void register_worktree(persistence::WorktreeRegistry& registry, persistence::Repository& repository,
                       std::filesystem::path const& path) {
  for (persistence::Repository const& entry : registry.repositories()) {
    for (persistence::Worktree const& worktree : entry.worktrees()) {
      if (worktree.path() == path.string()) {
        if (entry.root_path() != repository.root_path()) {
          throw std::invalid_argument("Worktree is already registered to another repository");
        }
        return;
      }
    }
  }
  repository.add_worktrees()->set_path(path.string());
}

std::string resolve_default_branch(std::string const& git_executable,
                                   std::filesystem::path const& bare_directory) {
  process::CommandResult const symbolic_ref = process::run_command(
      {git_executable, "--git-dir", bare_directory.string(), "symbolic-ref", "--quiet", "HEAD"},
      process::StandardOutputMode::CAPTURE);
  if (!symbolic_ref.exit_status.succeeded()) {
    throw std::runtime_error("resolve repository default branch failed with exit code " +
                             std::to_string(symbolic_ref.exit_status.value()));
  }
  std::string const reference = trimmed(symbolic_ref.standard_output);
  constexpr std::string_view PREFIX = "refs/heads/";
  if (!reference.starts_with(PREFIX) || reference.size() == PREFIX.size()) {
    throw std::runtime_error("repository does not expose a default branch");
  }

  process::CommandResult const commit =
      process::run_command({git_executable, "--git-dir", bare_directory.string(), "rev-parse",
                            "--verify", "--quiet", reference + "^{commit}"},
                           process::StandardOutputMode::CAPTURE);
  if (!commit.exit_status.succeeded() || trimmed(commit.standard_output).empty()) {
    throw std::runtime_error("repository default branch does not resolve to a commit");
  }
  return reference.substr(PREFIX.size());
}

bool resolves_to_commit(std::string const& git_executable,
                        std::filesystem::path const& bare_directory, std::string const& reference) {
  process::CommandResult const result =
      process::run_command({git_executable, "--git-dir", bare_directory.string(), "rev-parse",
                            "--verify", "--quiet", reference + "^{commit}"},
                           process::StandardOutputMode::CAPTURE);
  return result.exit_status.succeeded() && !trimmed(result.standard_output).empty();
}

}  // namespace

std::filesystem::path derived_worktree_path(std::filesystem::path const& repository_root,
                                            std::string const& branch) {
  validate_branch(branch);
  std::string directory_name = branch;
  std::ranges::replace(directory_name, '/', '-');
  return canonical_path(repository_root, "repository root") / directory_name;
}

WorktreeProvisioner::WorktreeProvisioner(std::string executable)
    : git_executable(std::move(executable)) {
  if (git_executable.empty()) {
    throw std::invalid_argument("Git executable must not be empty");
  }
}

WorktreeProvisionResult WorktreeProvisioner::provision(WorktreeProvisionRequest const& request,
                                                       std::ostream& progress) const {
  validate_branch(request.branch);
  std::filesystem::path const repository_root =
      canonical_path(request.repository_root, "repository root");
  std::filesystem::path const bare_directory = repository_root / ".bare";
  std::error_code error;
  if (!std::filesystem::is_directory(bare_directory, error) || error != std::error_code{}) {
    throw std::invalid_argument("Selected repository does not contain a .bare directory");
  }
  std::filesystem::path const worktree_path = validated_worktree_path(request, repository_root);

  WorktreeRegistryStore const store(request.registry_path);
  persistence::WorktreeRegistry registry = store.load();
  persistence::Repository* const repository = registered_repository(registry, repository_root);
  register_worktree(registry, *repository, worktree_path);

  bool const path_exists = std::filesystem::exists(worktree_path, error);
  if (error != std::error_code{}) {
    throw std::filesystem::filesystem_error("inspect worktree path", worktree_path, error);
  }
  if (path_exists) {
    validate_existing_worktree(worktree_path, repository_root, request.branch);
    store.save(registry);
    progress << "Worktree registered: " << worktree_path.string() << '\n';
    return {.worktree_path = worktree_path, .kind = WorktreeProvisionKind::ADOPTED};
  }

  std::string const remote_reference = "refs/remotes/origin/" + request.branch;
  std::string const fetch_refspec = "+refs/heads/" + request.branch + ":" + remote_reference;
  progress << "Fetching origin branch " << request.branch << "...\n";
  process::CommandResult const fetch = process::run_command(
      {git_executable, "--git-dir", bare_directory.string(), "fetch", "origin", fetch_refspec},
      process::StandardOutputMode::INHERIT);
  if (!fetch.exit_status.succeeded()) {
    progress << "Origin branch was not fetched; checking local refs...\n";
  }

  std::vector<std::string> create_command{git_executable, "--git-dir", bare_directory.string(),
                                          "worktree", "add"};
  std::string const local_reference = "refs/heads/" + request.branch;
  if (resolves_to_commit(git_executable, bare_directory, local_reference)) {
    progress << "Checking out existing branch " << request.branch << "...\n";
    create_command.push_back(worktree_path.string());
    create_command.push_back(request.branch);
  } else if (resolves_to_commit(git_executable, bare_directory, remote_reference)) {
    progress << "Creating tracking worktree for origin/" << request.branch << "...\n";
    create_command.insert(
        create_command.end(),
        {"--track", "-b", request.branch, worktree_path.string(), "origin/" + request.branch});
  } else {
    std::string const default_branch = resolve_default_branch(git_executable, bare_directory);
    progress << "Creating worktree from " << default_branch << "...\n";
    create_command.insert(create_command.end(),
                          {"-b", request.branch, worktree_path.string(), default_branch});
  }

  process::CommandResult const create =
      process::run_command(create_command, process::StandardOutputMode::INHERIT);
  if (!create.exit_status.succeeded()) {
    throw std::runtime_error("git worktree add failed with exit code " +
                             std::to_string(create.exit_status.value()));
  }
  if (!std::filesystem::is_directory(worktree_path, error) || error != std::error_code{} ||
      !std::filesystem::exists(worktree_path / ".git", error) || error != std::error_code{}) {
    throw std::runtime_error("git worktree add succeeded without creating a valid worktree");
  }

  try {
    store.save(registry);
  } catch (std::exception const& exception) {
    throw std::runtime_error("worktree was created but registry update failed: " +
                             std::string(exception.what()));
  }
  progress << "Worktree created: " << worktree_path.string() << '\n';
  return {.worktree_path = worktree_path, .kind = WorktreeProvisionKind::CREATED};
}

}  // namespace moe::parent
