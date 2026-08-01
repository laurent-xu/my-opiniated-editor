#pragma once

#include <filesystem>
#include <string>

namespace moe::parent {

struct WorktreeProvisionRequest {
  std::filesystem::path repository_root;
  std::string branch;
  std::filesystem::path worktree_path;
  std::filesystem::path registry_path;
};

}  // namespace moe::parent
