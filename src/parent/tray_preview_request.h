#pragma once

#include "src/base/terminal_size.h"
#include "src/parent/terminal_position.h"
#include "src/parent/tray_id.h"

namespace moe::parent {

struct TrayPreviewRequest {
  TrayId tray_id;
  TerminalPosition origin;
  base::TerminalSize size;
};

}  // namespace moe::parent
