#pragma once

#include "src/parent/tray/tray_id.h"
#include "src/parent/tray_action_kind.h"

namespace moe::parent {

struct TrayActionRequest {
  TrayActionKind kind;
  TrayId tray_id;
};

}  // namespace moe::parent
