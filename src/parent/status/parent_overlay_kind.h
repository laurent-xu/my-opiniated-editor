#pragma once

#include <cstdint>

namespace moe::parent {

enum class ParentOverlayKind : std::uint8_t {
  NONE,
  WORKTREE_MANAGEMENT,
};

}  // namespace moe::parent
