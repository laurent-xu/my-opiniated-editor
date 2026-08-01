#pragma once

#include "src/parent/status/parent_overlay_kind.h"
#include "src/parent/tray/tray_id.h"

namespace moe::parent {

struct ParentStatus {
  bool command_mode;
  TrayId active_tray;
  ParentOverlayKind overlay;
};

}  // namespace moe::parent
