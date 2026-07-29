#include "src/parent/worktree_repository_registrar.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
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

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"
#include "src/base/process_id.h"
#include "src/parent/git_worktree_list.h"
#include "src/parent/worktree_registry_store.h"

namespace moe::parent {
namespace {

constexpr std::string_view GIT_POINTER_CONTENT = "gitdir: ./.bare";
constexpr std::string_view REMOTE_FETCH_REFSPEC = "+refs/heads/*:refs/remotes/origin/*";

struct CommandResult {
  int exit_code;
  std::string standard_output;
};

std::runtime_error errno_error(std::string const& action) {
  return std::runtime_error(action + ": " + std::generic_category().message(errno));
}

base::ProcessId wait_for_child(base::ProcessId const child_pid, int& status) {
  base::ProcessId result;
  do {
    result = base::ProcessId(::waitpid(child_pid.value(), &status, 0));
  } while (result.is_error() && errno == EINTR);
  return result;
}

int exit_code_from_status(int const status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 1;
}

std::vector<char*> command_argv(std::vector<std::string> const& command) {
  std::vector<char*> argv;
  argv.reserve(command.size() + 1);
  for (std::string const& argument : command) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);
  return argv;
}

CommandResult run_command(std::vector<std::string> const& command,
                          bool const capture_standard_output) {
  if (command.empty()) {
    throw std::invalid_argument("command must not be empty");
  }

  std::array<int, 2> raw_pipe{-1, -1};
  if (capture_standard_output && ::pipe(raw_pipe.data()) != 0) {
    throw errno_error("create git output pipe");
  }
  base::OwnedFileDescriptor read_end(capture_standard_output ? base::FileDescriptor(raw_pipe[0])
                                                             : base::FileDescriptor{});
  base::OwnedFileDescriptor write_end(capture_standard_output ? base::FileDescriptor(raw_pipe[1])
                                                              : base::FileDescriptor{});

  base::ProcessId const child_pid(::fork());
  if (child_pid.is_error()) {
    throw errno_error("fork git command");
  }
  if (child_pid.is_child_process()) {
    if (capture_standard_output) {
      read_end.reset();
      if (::dup2(write_end.get().value(), STDOUT_FILENO) < 0) {
        _exit(126);
      }
      write_end.reset();
    }

    std::vector<char*> argv = command_argv(command);
    ::execvp(argv[0], argv.data());
    _exit(127);
  }

  write_end.reset();
  std::string output;
  if (capture_standard_output) {
    std::array<char, 4096> buffer{};
    while (true) {
      ssize_t const count = ::read(read_end.get().value(), buffer.data(), buffer.size());
      if (count > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(count));
        continue;
      }
      if (count == 0) {
        break;
      }
      if (errno != EINTR) {
        throw errno_error("read git command output");
      }
    }
  }

  int status = 0;
  base::ProcessId const waited = wait_for_child(child_pid, status);
  if (waited.value() != child_pid.value()) {
    throw errno_error("wait for git command");
  }
  return {.exit_code = exit_code_from_status(status), .standard_output = std::move(output)};
}

void require_success(CommandResult const& result, std::string const& action) {
  if (result.exit_code != 0) {
    throw std::runtime_error(action + " failed with exit code " + std::to_string(result.exit_code));
  }
}

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
      trimmed(read_git_pointer(pointer_path)) != GIT_POINTER_CONTENT) {
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
    if (!request.clone_url.has_value() || trimmed(*request.clone_url).empty()) {
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
    CommandResult const clone = run_command(
        {git_executable, "clone", "--bare", trimmed(*request.clone_url), (root / ".bare").string()},
        false);
    if (clone.exit_code != 0) {
      std::error_code cleanup_error;
      std::filesystem::remove_all(root / ".bare", cleanup_error);
      if (created_root) {
        std::filesystem::remove(root, cleanup_error);
      }
      throw std::runtime_error("git clone --bare failed with exit code " +
                               std::to_string(clone.exit_code));
    }
    cloned = true;
    state = RepositoryRootState::RECOVERABLE_BARE_ROOT;
  }

  if (state == RepositoryRootState::RECOVERABLE_BARE_ROOT) {
    write_git_pointer(root);
  }

  std::filesystem::path const bare_directory = root / ".bare";
  CommandResult const bare_check = run_command(
      {git_executable, "--git-dir", bare_directory.string(), "rev-parse", "--is-bare-repository"},
      true);
  require_success(bare_check, "validate bare repository");
  if (trimmed(bare_check.standard_output) != "true") {
    throw std::runtime_error("repository .bare directory is not a bare Git repository");
  }

  if (cloned || state == RepositoryRootState::RECOVERABLE_BARE_ROOT) {
    progress << "Configuring remote branches...\n";
    require_success(run_command({git_executable, "--git-dir", bare_directory.string(), "config",
                                 "remote.origin.fetch", std::string(REMOTE_FETCH_REFSPEC)},
                                false),
                    "configure remote fetch");
    progress << "Fetching remote branches...\n";
    require_success(
        run_command({git_executable, "--git-dir", bare_directory.string(), "fetch", "origin"},
                    false),
        "fetch remote branches");
  }

  CommandResult const default_branch = run_command(
      {git_executable, "--git-dir", bare_directory.string(), "symbolic-ref", "--quiet", "HEAD"},
      true);
  require_success(default_branch, "resolve default branch");
  std::string const default_branch_ref = trimmed(default_branch.standard_output);
  if (!default_branch_ref.starts_with("refs/heads/")) {
    throw std::runtime_error("repository does not expose a default branch");
  }
  CommandResult const default_branch_commit =
      run_command({git_executable, "--git-dir", bare_directory.string(), "rev-parse", "--verify",
                   "--quiet", default_branch_ref + "^{commit}"},
                  true);
  require_success(default_branch_commit, "resolve default branch commit");
  if (trimmed(default_branch_commit.standard_output).empty()) {
    throw std::runtime_error("repository default branch does not resolve to a commit");
  }

  CommandResult const worktree_list =
      run_command({git_executable, "--git-dir", bare_directory.string(), "worktree", "list",
                   "--porcelain", "-z"},
                  true);
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
