#include "src/parent/worktree/git_worktree_list.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace moe::parent {
namespace {

std::filesystem::path normalized_absolute_path(std::filesystem::path const& path) {
  if (path.empty() || !path.is_absolute()) {
    throw std::invalid_argument("git worktree path must be an absolute path");
  }
  std::error_code error;
  std::filesystem::path const normalized = std::filesystem::weakly_canonical(path, error);
  if (error != std::error_code{}) {
    throw std::invalid_argument("failed to normalize git worktree path: " + path.string());
  }
  return normalized;
}

}  // namespace

std::vector<GitWorktreeListEntry> parse_git_worktree_list(std::string_view const porcelain_output) {
  constexpr std::string_view WORKTREE_PREFIX = "worktree ";
  constexpr std::string_view BRANCH_PREFIX = "branch refs/heads/";
  std::vector<GitWorktreeListEntry> entries;
  std::optional<GitWorktreeListEntry> current;

  auto finish_record = [&]() {
    if (current.has_value() && !current->path.empty()) {
      entries.push_back(std::move(*current));
    }
    current.reset();
  };

  std::size_t start = 0;
  while (start <= porcelain_output.size()) {
    std::size_t const end = porcelain_output.find('\0', start);
    std::string_view const field(
        porcelain_output.data() + start,
        (end == std::string_view::npos ? porcelain_output.size() : end) - start);
    if (field.empty()) {
      finish_record();
    } else if (field.starts_with(WORKTREE_PREFIX)) {
      finish_record();
      current =
          GitWorktreeListEntry{.path = std::filesystem::path(field.substr(WORKTREE_PREFIX.size()))};
    } else if (current.has_value() && field == "bare") {
      current->bare = true;
    } else if (current.has_value() && field.starts_with(BRANCH_PREFIX)) {
      current->branch = field.substr(BRANCH_PREFIX.size());
    } else if (current.has_value() && field.starts_with("prunable")) {
      current->prunable = true;
    }

    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  finish_record();
  return entries;
}

std::vector<std::filesystem::path> available_git_worktree_paths(
    std::string_view const porcelain_output) {
  std::vector<std::filesystem::path> paths;
  for (GitWorktreeListEntry const& entry : parse_git_worktree_list(porcelain_output)) {
    if (entry.bare || entry.prunable) {
      continue;
    }
    std::filesystem::path const normalized = normalized_absolute_path(entry.path);
    std::error_code error;
    bool const exists = std::filesystem::exists(normalized, error);
    if (error != std::error_code{}) {
      throw std::filesystem::filesystem_error("inspect git worktree", normalized, error);
    }
    if (exists && std::filesystem::is_directory(normalized, error)) {
      paths.push_back(normalized);
    }
    if (error != std::error_code{}) {
      throw std::filesystem::filesystem_error("inspect git worktree", normalized, error);
    }
  }

  std::ranges::sort(paths);
  auto const unique_end = std::ranges::unique(paths).begin();
  paths.erase(unique_end, paths.end());
  return paths;
}

}  // namespace moe::parent
