#pragma once

#include <cstdint>

#include "src/parent/pane/pane_selection.h"

namespace moe::parent {

enum class PaneDropDirection : std::uint8_t {
  LEFT,
  RIGHT,
  UP,
  DOWN,
};

[[nodiscard]] bool move_pane_selection(PaneLayout& layout, PaneSelection const& selection,
                                       PaneNodeId target, PaneDropDirection direction);
[[nodiscard]] bool swap_pane_nodes(PaneLayout& layout, PaneNodeId source, PaneNodeId target);

}  // namespace moe::parent
