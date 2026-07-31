#pragma once

#include <filesystem>

namespace moe::parent {

struct WorktreeRemovalRequest {
  std::filesystem::path registry_path;
  std::filesystem::path worktree_path;
};

}  // namespace moe::parent
