#pragma once

#include <cstdint>
#include <optional>

#include "src/parent/pane/pane_geometry.h"

namespace moe::parent {

enum class PaneFocusDirection : std::uint8_t {
  LEFT,
  RIGHT,
  UP,
  DOWN,
};

[[nodiscard]] std::optional<PaneNodeId> find_directional_pane(PaneLayout const& layout,
                                                              PaneGeometry const& geometry,
                                                              PaneNodeId source,
                                                              PaneFocusDirection direction);

}  // namespace moe::parent
