#pragma once

#include <optional>
#include <string>

#include "src/base/terminal_size.h"
#include "src/parent/pane/pane_layout.h"
#include "src/parent/pane/pane_move_session.h"
#include "src/parent/pane/pane_selection.h"

namespace moe::parent {

struct ParentPaneView {
  PaneLayout const& layout;
  PaneId focused_pane;
  bool maximized;
  std::optional<PaneSelection> const& selection;
  std::optional<PaneMoveSession> const& move;
};

struct ParentPanePreview {
  std::string tray_key;
  int origin_row;
  int origin_column;
  base::TerminalSize size;
  PaneLayout const& layout;
  PaneId focused_pane;
  bool maximized;
};

}  // namespace moe::parent
