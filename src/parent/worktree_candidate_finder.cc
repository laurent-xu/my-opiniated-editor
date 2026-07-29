#include "src/parent/worktree_candidate_finder.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
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

struct CommandResult {
  int exit_code;
  std::string standard_output;
};

std::runtime_error errno_error(std::string const& action) {
  return std::runtime_error(action + ": " + std::generic_category().message(errno));
}

std::vector<char*> command_argv(std::vector<std::string> const& command) {
  std::vector<char*> arguments;
  arguments.reserve(command.size() + 1);
  for (std::string const& part : command) {
    arguments.push_back(const_cast<char*>(part.c_str()));
  }
  arguments.push_back(nullptr);
  return arguments;
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

CommandResult run_command(std::vector<std::string> const& command) {
  std::array<int, 2> raw_pipe{-1, -1};
  if (::pipe(raw_pipe.data()) != 0) {
    throw errno_error("create git worktree-list pipe");
  }
  base::OwnedFileDescriptor read_end{base::FileDescriptor(raw_pipe[0])};
  base::OwnedFileDescriptor write_end{base::FileDescriptor(raw_pipe[1])};

  base::ProcessId const child_pid(::fork());
  if (child_pid.is_error()) {
    throw errno_error("fork git worktree-list command");
  }
  if (child_pid.is_child_process()) {
    read_end.reset();
    if (::dup2(write_end.get().value(), STDOUT_FILENO) < 0) {
      _exit(126);
    }
    write_end.reset();
    std::vector<char*> arguments = command_argv(command);
    ::execvp(arguments[0], arguments.data());
    _exit(127);
  }

  write_end.reset();
  std::string output;
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
      throw errno_error("read git worktree-list output");
    }
  }

  int status = 0;
  base::ProcessId waited;
  do {
    waited = base::ProcessId(::waitpid(child_pid.value(), &status, 0));
  } while (waited.is_error() && errno == EINTR);
  if (waited.value() != child_pid.value()) {
    throw errno_error("wait for git worktree-list command");
  }
  return {.exit_code = exit_code_from_status(status), .standard_output = std::move(output)};
}

bool is_existing_directory(std::filesystem::path const& path) {
  std::error_code error;
  bool const result = std::filesystem::is_directory(path, error);
  return error == std::error_code{} && result;
}

bool has_git_marker(std::filesystem::path const& worktree) {
  std::error_code error;
  bool const exists = std::filesystem::exists(worktree / ".git", error);
  return error == std::error_code{} && exists;
}

}  // namespace

WorktreeCandidateFinder::WorktreeCandidateFinder(std::string executable)
    : git_executable(std::move(executable)) {
  if (git_executable.empty()) {
    throw std::invalid_argument("git executable must not be empty");
  }
}

std::vector<std::filesystem::path> WorktreeCandidateFinder::find_available(
    std::filesystem::path const& registry_path) const {
  persistence::WorktreeRegistry const registry = WorktreeRegistryStore(registry_path).load();
  std::set<std::filesystem::path> candidates;

  for (persistence::Repository const& repository : registry.repositories()) {
    std::filesystem::path const repository_root(repository.root_path());
    std::filesystem::path const bare_directory = repository_root / ".bare";
    if (!is_existing_directory(repository_root) || !is_existing_directory(bare_directory)) {
      continue;
    }

    CommandResult const result = run_command({git_executable, "--git-dir", bare_directory.string(),
                                              "worktree", "list", "--porcelain", "-z"});
    if (result.exit_code != 0) {
      continue;
    }

    std::vector<std::filesystem::path> const live_paths =
        available_git_worktree_paths(result.standard_output);
    std::set<std::filesystem::path> const live_worktrees(live_paths.begin(), live_paths.end());
    for (persistence::Worktree const& tracked : repository.worktrees()) {
      std::filesystem::path const tracked_path(tracked.path());
      if (live_worktrees.contains(tracked_path) && has_git_marker(tracked_path)) {
        candidates.insert(tracked_path);
      }
    }
  }

  return {candidates.begin(), candidates.end()};
}

}  // namespace moe::parent
