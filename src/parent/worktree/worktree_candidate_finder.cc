#include "src/parent/worktree/worktree_candidate_finder.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "src/parent/worktree/git_worktree_list.h"
#include "src/parent/worktree/worktree_registry_store.h"
#include "src/process/command_runner.h"

namespace moe::parent {
namespace {

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

    process::CommandResult const result =
        process::run_command({git_executable, "--git-dir", bare_directory.string(), "worktree",
                              "list", "--porcelain", "-z"},
                             process::StandardOutputMode::CAPTURE);
    if (!result.exit_status.succeeded()) {
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
