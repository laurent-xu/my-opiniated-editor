#pragma once

#include <map>
#include <vector>

#include "src/base/terminal_size.h"
#include "src/parent/pane/pane_layout.h"
#include "src/parent/terminal/screen/terminal_position.h"

namespace moe::parent {

class PaneGeometryCalculator;

struct PaneRegion {
  TerminalPosition origin;
  base::TerminalSize size;
};

struct PaneSeparator {
  PaneNodeId split_node;
  PaneSplitAxis axis;
  PaneRegion region;
};

class PaneGeometry {
 public:
  [[nodiscard]] PaneRegion const& region(PaneNodeId node_id) const;
  [[nodiscard]] std::vector<PaneSeparator> const& separators() const;

 private:
  friend PaneGeometry calculate_pane_geometry(PaneLayout const& layout, PaneRegion outer_region);
  friend class PaneGeometryCalculator;

  std::map<PaneNodeId, PaneRegion> node_regions;
  std::vector<PaneSeparator> split_separators;
};

[[nodiscard]] PaneGeometry calculate_pane_geometry(PaneLayout const& layout,
                                                   PaneRegion outer_region);

}  // namespace moe::parent
