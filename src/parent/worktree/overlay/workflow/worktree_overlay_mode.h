#pragma once

#include <cstdint>

namespace moe::parent {

enum class WorktreeOverlayMode : std::uint8_t {
  SWITCH_WORKTREE,
  ADD_WORKTREE,
  ADD_REPOSITORY,
};

}  // namespace moe::parent
