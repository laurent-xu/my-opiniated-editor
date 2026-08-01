#pragma once

#include "src/parent/pane/pane_layout.h"
#include "src/parent/pane/pane_selection.h"

namespace moe::parent {

[[nodiscard]] bool resize_pane_selection(PaneLayout& layout, PaneSelection const& selection,
                                         int delta_percentage);
[[nodiscard]] bool equalize_pane_selection(PaneLayout& layout, PaneSelection const& selection);
[[nodiscard]] bool equalize_pane_selection_level(PaneLayout& layout,
                                                 PaneSelection const& selection);

}  // namespace moe::parent
