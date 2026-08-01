#pragma once

#include <cstdint>
#include <string>

#include "src/parent/tray/tray_id.h"

namespace moe::parent {

enum class ParentOverlayKind : std::uint8_t {
  NONE,
  WORKTREE_MANAGEMENT,
};

struct ParentStatus {
  bool command_mode;
  TrayId active_tray;
  ParentOverlayKind overlay;
};

[[nodiscard]] std::string serialize_parent_status(ParentStatus const& status);

}  // namespace moe::parent
