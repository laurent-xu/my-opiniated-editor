#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "src/parent/pane/pane_layout.h"

namespace moe::parent {

enum class PaneSiblingDirection : std::uint8_t {
  PREVIOUS,
  NEXT,
};

class PaneSelection {
 public:
  [[nodiscard]] static PaneSelection single(PaneLayout const& layout, PaneNodeId node_id);
  [[nodiscard]] static PaneSelection range(PaneLayout const& layout, PaneNodeId anchor,
                                           PaneNodeId active);

  [[nodiscard]] std::optional<PaneNodeId> parent() const;
  [[nodiscard]] PaneNodeId anchor() const;
  [[nodiscard]] PaneNodeId active() const;
  [[nodiscard]] std::vector<PaneNodeId> const& nodes() const;
  [[nodiscard]] bool contains(PaneNodeId node_id) const;

  [[nodiscard]] PaneSelection step(PaneLayout const& layout, PaneSiblingDirection direction) const;
  [[nodiscard]] PaneSelection promote(PaneLayout const& layout) const;
  [[nodiscard]] PaneSelection descend(PaneLayout const& layout) const;

 private:
  struct Endpoints {
    PaneNodeId anchor;
    PaneNodeId active;
  };

  PaneSelection(std::optional<PaneNodeId> parent, Endpoints endpoints,
                std::vector<PaneNodeId> nodes);

  std::optional<PaneNodeId> parent_id;
  PaneNodeId anchor_id;
  PaneNodeId active_id;
  std::vector<PaneNodeId> selected_nodes;
};

}  // namespace moe::parent
