#include "src/parent/worktree/removal/worktree_remover.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "src/base/process_id.h"
#include "src/parent/worktree/git_worktree_list.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"
#include "src/process/process_exit_status.h"

namespace moe::parent {
namespace {

std::vector<char*> command_argv(std::vector<std::string> const& command) {
  std::vector<char*> arguments;
  arguments.reserve(command.size() + 1U);
  for (std::string const& part : command) {
    arguments.push_back(const_cast<char*>(part.c_str()));
  }
  arguments.push_back(nullptr);
  return arguments;
}

process::ProcessExitStatus run_command(std::vector<std::string> const& command) {
  base::ProcessId const child_pid(::fork());
  if (child_pid.is_error()) {
    throw std::system_error(errno, std::generic_category(), "fork Git worktree removal command");
  }
  if (child_pid.is_child_process()) {
    int const null_descriptor = ::open("/dev/null", O_WRONLY);
    if (null_descriptor < 0 || ::dup2(null_descriptor, STDOUT_FILENO) < 0 ||
        ::dup2(null_descriptor, STDERR_FILENO) < 0) {
      _exit(126);
    }
    static_cast<void>(::close(null_descriptor));
    std::vector<char*> arguments = command_argv(command);
    ::execvp(arguments[0], arguments.data());
    _exit(127);
  }

  int status = 0;
  base::ProcessId waited;
  do {
    waited = base::ProcessId(::waitpid(child_pid.value(), &status, 0));
  } while (waited.is_error() && errno == EINTR);
  if (waited != child_pid) {
    throw std::system_error(errno, std::generic_category(), "wait for Git worktree removal");
  }
  return process::ProcessExitStatus::from_wait_status(process::ProcessWaitStatus(status));
}

std::filesystem::path normalized_path(std::filesystem::path const& path,
                                      std::string const& description) {
  if (path.empty() || !path.is_absolute()) {
    throw std::invalid_argument(description + " must be an absolute path");
  }
  std::error_code error;
  std::filesystem::path const normalized = std::filesystem::weakly_canonical(path, error);
  if (error != std::error_code{}) {
    throw std::invalid_argument("failed to normalize " + description + ": " + path.string());
  }
  return normalized;
}

struct TrackedWorktree {
  int repository_index;
  int worktree_index;
  std::filesystem::path repository_root;
};

TrackedWorktree find_tracked_worktree(persistence::WorktreeRegistry const& registry,
                                      std::filesystem::path const& worktree_path) {
  for (int repository_index = 0; repository_index < registry.repositories_size();
       ++repository_index) {
    persistence::Repository const& repository = registry.repositories(repository_index);
    for (int worktree_index = 0; worktree_index < repository.worktrees_size(); ++worktree_index) {
      if (repository.worktrees(worktree_index).path() == worktree_path.string()) {
        return {
            .repository_index = repository_index,
            .worktree_index = worktree_index,
            .repository_root = repository.root_path(),
        };
      }
    }
  }
  throw std::invalid_argument("Worktree is not tracked: " + worktree_path.string());
}

struct GitWorktreeQuery {
  std::filesystem::path bare_directory;
  std::filesystem::path worktree_path;
};

bool git_lists_worktree(std::string const& git_executable, GitWorktreeQuery const& query) {
  std::array<int, 2> raw_pipe{-1, -1};
  if (::pipe(raw_pipe.data()) != 0) {
    throw std::system_error(errno, std::generic_category(), "create Git worktree-list pipe");
  }

  base::ProcessId const child_pid(::fork());
  if (child_pid.is_error()) {
    static_cast<void>(::close(raw_pipe[0]));
    static_cast<void>(::close(raw_pipe[1]));
    throw std::system_error(errno, std::generic_category(), "fork Git worktree-list command");
  }
  if (child_pid.is_child_process()) {
    static_cast<void>(::close(raw_pipe[0]));
    if (::dup2(raw_pipe[1], STDOUT_FILENO) < 0) {
      _exit(126);
    }
    static_cast<void>(::close(raw_pipe[1]));
    int const null_descriptor = ::open("/dev/null", O_WRONLY);
    if (null_descriptor < 0 || ::dup2(null_descriptor, STDERR_FILENO) < 0) {
      _exit(126);
    }
    static_cast<void>(::close(null_descriptor));
    std::vector<std::string> const command{
        git_executable, "--git-dir", query.bare_directory.string(), "worktree", "list",
        "--porcelain",  "-z"};
    std::vector<char*> arguments = command_argv(command);
    ::execvp(arguments[0], arguments.data());
    _exit(127);
  }

  static_cast<void>(::close(raw_pipe[1]));
  std::string output;
  std::array<char, 4096> buffer{};
  while (true) {
    ssize_t const count = ::read(raw_pipe[0], buffer.data(), buffer.size());
    if (count > 0) {
      output.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) {
      break;
    }
    if (errno != EINTR) {
      int const error = errno;
      static_cast<void>(::close(raw_pipe[0]));
      throw std::system_error(error, std::generic_category(), "read Git worktree list");
    }
  }
  static_cast<void>(::close(raw_pipe[0]));

  int status = 0;
  base::ProcessId waited;
  do {
    waited = base::ProcessId(::waitpid(child_pid.value(), &status, 0));
  } while (waited.is_error() && errno == EINTR);
  if (waited != child_pid) {
    throw std::system_error(errno, std::generic_category(), "wait for Git worktree list");
  }
  process::ProcessExitStatus const exit_status =
      process::ProcessExitStatus::from_wait_status(process::ProcessWaitStatus(status));
  if (!exit_status.succeeded()) {
    throw std::runtime_error("git worktree list failed with exit code " +
                             std::to_string(exit_status.value()));
  }

  std::vector<GitWorktreeListEntry> const entries = parse_git_worktree_list(output);
  return std::ranges::any_of(entries, [&query](GitWorktreeListEntry const& entry) {
    return normalized_path(entry.path, "Git worktree path") == query.worktree_path;
  });
}

bool path_exists(std::filesystem::path const& path) {
  std::error_code error;
  bool const exists = std::filesystem::exists(path, error);
  if (error != std::error_code{}) {
    throw std::filesystem::filesystem_error("inspect worktree path", path, error);
  }
  return exists;
}

}  // namespace

WorktreeRemover::WorktreeRemover(std::string executable) : git_executable(std::move(executable)) {
  if (git_executable.empty()) {
    throw std::invalid_argument("Git executable must not be empty");
  }
}

void WorktreeRemover::remove(WorktreeRemovalRequest const& request) const {
  std::filesystem::path const worktree_path =
      normalized_path(request.worktree_path, "worktree path");
  std::filesystem::path const protected_worktree_path =
      normalized_path(request.protected_worktree_path, "protected worktree path");
  if (worktree_path == protected_worktree_path) {
    throw std::runtime_error("protected worktree runs my-opiniated-editor and cannot be removed: " +
                             worktree_path.string());
  }
  WorktreeRegistryStore const store(request.registry_path);
  persistence::WorktreeRegistry registry = store.load();
  TrackedWorktree const tracked = find_tracked_worktree(registry, worktree_path);
  std::filesystem::path const bare_directory = tracked.repository_root / ".bare";

  bool git_owned_worktree = false;
  if (path_exists(bare_directory)) {
    GitWorktreeQuery const query{
        .bare_directory = bare_directory,
        .worktree_path = worktree_path,
    };
    git_owned_worktree = git_lists_worktree(git_executable, query);
    if (git_owned_worktree) {
      process::ProcessExitStatus const remove_status =
          run_command({git_executable, "--git-dir", bare_directory.string(), "worktree", "remove",
                       "--force", "--force", worktree_path.string()});
      if (!remove_status.succeeded()) {
        if (path_exists(worktree_path)) {
          throw std::runtime_error("git worktree remove failed with exit code " +
                                   std::to_string(remove_status.value()));
        }
        process::ProcessExitStatus const prune_status =
            run_command({git_executable, "--git-dir", bare_directory.string(), "worktree", "prune",
                         "--expire", "now"});
        if (!prune_status.succeeded() || git_lists_worktree(git_executable, query)) {
          throw std::runtime_error("git worktree purge failed for missing worktree");
        }
      }
    }
  }

  persistence::Repository* const repository =
      registry.mutable_repositories(tracked.repository_index);
  repository->mutable_worktrees()->DeleteSubrange(tracked.worktree_index, 1);
  try {
    store.save(registry);
  } catch (std::exception const& error) {
    if (git_owned_worktree) {
      throw std::runtime_error("worktree was purged but registry update failed: " +
                               std::string(error.what()));
    }
    throw;
  }
}

}  // namespace moe::parent
