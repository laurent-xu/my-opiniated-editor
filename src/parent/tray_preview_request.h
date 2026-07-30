#pragma once

#include "src/parent/content_pty_session.h"
#include "src/parent/terminal_position.h"
#include "src/parent/tray_id.h"

namespace moe::parent {

struct TrayPreviewRequest {
  TrayId tray_id;
  TerminalPosition origin;
  TerminalSize size;
};

}  // namespace moe::parent
