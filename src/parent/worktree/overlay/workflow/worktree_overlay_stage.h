#pragma once

#include <cstdint>

namespace moe::parent {

enum class WorktreeOverlayStage : std::uint8_t {
  SWITCH_WORKTREE,
  WORKTREE_REPOSITORY,
  WORKTREE_BRANCH,
  REPOSITORY_ROOT,
  REPOSITORY_CLONE_URL,
  RUNNING,
  RESULT,
};

}  // namespace moe::parent
