#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "src/parent/pane/pane_geometry.h"

namespace moe::parent {

enum class PaneFocusDirection : std::uint8_t {
  LEFT,
  RIGHT,
  UP,
  DOWN,
};

using PaneNodeEligibility = std::function<bool(PaneNodeId)>;

[[nodiscard]] std::optional<PaneNodeId> find_directional_pane(PaneLayout const& layout,
                                                              PaneGeometry const& geometry,
                                                              PaneNodeId source,
                                                              PaneFocusDirection direction);
[[nodiscard]] std::optional<PaneNodeId> find_directional_pane(PaneLayout const& layout,
                                                              PaneGeometry const& geometry,
                                                              PaneNodeId source,
                                                              PaneFocusDirection direction,
                                                              PaneNodeEligibility const& eligible);
[[nodiscard]] std::optional<PaneNodeId> find_parent_pane_node(PaneLayout const& layout,
                                                              PaneNodeId source);
[[nodiscard]] std::optional<PaneNodeId> find_parent_pane_node(PaneLayout const& layout,
                                                              PaneNodeId source,
                                                              PaneNodeEligibility const& eligible);
[[nodiscard]] std::optional<PaneNodeId> find_first_child_pane_node(PaneLayout const& layout,
                                                                   PaneNodeId source);
[[nodiscard]] std::optional<PaneNodeId> find_first_child_pane_node(
    PaneLayout const& layout, PaneNodeId source, PaneNodeEligibility const& eligible);

}  // namespace moe::parent
