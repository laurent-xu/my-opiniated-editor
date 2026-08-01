#pragma once

#include <filesystem>
#include <string>

namespace moe::parent {

struct ParentCommandDispatcherConfig {
  std::filesystem::path parent_executable;
  std::filesystem::path worktree_registry_path;
  std::string git_executable;
  std::string fzf_executable;
};

}  // namespace moe::parent
