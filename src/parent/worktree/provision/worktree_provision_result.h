#pragma once

#include <filesystem>

#include "src/parent/worktree/provision/worktree_provision_kind.h"

namespace moe::parent {

struct WorktreeProvisionResult {
  std::filesystem::path worktree_path;
  WorktreeProvisionKind kind;
};

}  // namespace moe::parent
