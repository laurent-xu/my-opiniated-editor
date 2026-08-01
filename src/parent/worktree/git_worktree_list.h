#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moe::parent {

struct GitWorktreeListEntry {
  std::filesystem::path path;
  std::optional<std::string> branch;
  bool bare = false;
  bool prunable = false;
};

[[nodiscard]] std::vector<GitWorktreeListEntry> parse_git_worktree_list(
    std::string_view porcelain_output);
[[nodiscard]] std::vector<std::filesystem::path> available_git_worktree_paths(
    std::string_view porcelain_output);

}  // namespace moe::parent
