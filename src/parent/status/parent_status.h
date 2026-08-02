#pragma once

#include <cstddef>
#include <cstdint>

#include "src/parent/status/parent_overlay_kind.h"
#include "src/parent/tray/tray_id.h"

namespace moe::parent {

enum class ParentPaneMode : std::uint8_t {
  NONE,
  SELECTION,
  MOVE_TARGET,
  MOVE_DROP,
  SWAP_TARGET,
};

struct ParentStatus {
  bool command_mode;
  TrayId active_tray;
  ParentOverlayKind overlay;
  ParentPaneMode pane_mode;
  std::size_t pane_selected_nodes;
};

}  // namespace moe::parent
