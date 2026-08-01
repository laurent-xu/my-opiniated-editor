#pragma once

#include "src/parent/tray/tray_action_kind.h"
#include "src/parent/tray/tray_id.h"

namespace moe::parent {

struct TrayActionRequest {
  TrayActionKind kind;
  TrayId tray_id;
};

}  // namespace moe::parent
