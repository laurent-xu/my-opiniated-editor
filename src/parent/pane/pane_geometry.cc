#include "src/parent/pane/pane_geometry.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace moe::parent {

namespace {

std::vector<int> allocate_extent(std::vector<PaneSplitChild> const& children, int const extent) {
  int const percentage_sum = std::accumulate(children.begin(), children.end(), 0,
                                             [](int const total, PaneSplitChild const& child) {
                                               return total + child.percentage.value();
                                             });
  if (percentage_sum != PanePercentage::MAX_VALUE) {
    throw std::logic_error("pane percentages must sum to 100 before geometry calculation");
  }

  std::vector<int> sizes(children.size(), 0);
  std::vector<int> remainders(children.size(), 0);
  int assigned = 0;
  for (std::size_t index = 0; index < children.size(); ++index) {
    int const scaled = children[index].percentage.value() * extent;
    sizes[index] = scaled / PanePercentage::MAX_VALUE;
    remainders[index] = scaled % PanePercentage::MAX_VALUE;
    assigned += sizes[index];
  }

  std::vector<std::size_t> remainder_order(children.size());
  for (std::size_t index = 0; index < remainder_order.size(); ++index) {
    remainder_order[index] = index;
  }
  std::ranges::stable_sort(remainder_order,
                           [&remainders](std::size_t const lhs, std::size_t const rhs) {
                             return remainders[lhs] > remainders[rhs];
                           });
  for (int offset = 0; offset < extent - assigned; ++offset) {
    ++sizes[remainder_order[static_cast<std::size_t>(offset)]];
  }
  return sizes;
}

}  // namespace

class PaneGeometryCalculator {
 public:
  PaneGeometryCalculator(PaneLayout const& source_layout, PaneGeometry& destination)
      : layout(source_layout), geometry(destination) {}

  void append(PaneNodeId const node_id, PaneRegion const region) {
    geometry.node_regions.emplace(node_id, region);
    PaneLayoutNode const& node = layout.node(node_id);
    if (node.is_leaf()) {
      return;
    }

    PaneSplit const& split = node.split();
    int const full_extent =
        split.axis == PaneSplitAxis::LEFT_TO_RIGHT ? region.size.cols : region.size.rows;
    int const requested_separator_count = static_cast<int>(split.children.size()) - 1;
    int const separator_count =
        full_extent >= requested_separator_count ? requested_separator_count : 0;
    int const content_extent = full_extent - separator_count;
    std::vector<int> const child_extents = allocate_extent(split.children, content_extent);

    int cursor =
        split.axis == PaneSplitAxis::LEFT_TO_RIGHT ? region.origin.column : region.origin.row;
    for (std::size_t index = 0; index < split.children.size(); ++index) {
      PaneRegion child_region = region;
      if (split.axis == PaneSplitAxis::LEFT_TO_RIGHT) {
        child_region.origin.column = cursor;
        child_region.size.cols = child_extents[index];
      } else {
        child_region.origin.row = cursor;
        child_region.size.rows = child_extents[index];
      }
      append(split.children[index].node_id, child_region);
      cursor += child_extents[index];

      if (separator_count > 0 && index + 1 < split.children.size()) {
        PaneRegion separator_region = region;
        if (split.axis == PaneSplitAxis::LEFT_TO_RIGHT) {
          separator_region.origin.column = cursor;
          separator_region.size.cols = 1;
        } else {
          separator_region.origin.row = cursor;
          separator_region.size.rows = 1;
        }
        geometry.split_separators.push_back(PaneSeparator{
            .split_node = node_id,
            .axis = split.axis,
            .region = separator_region,
        });
        ++cursor;
      }
    }
  }

 private:
  PaneLayout const& layout;
  PaneGeometry& geometry;
};

PaneRegion const& PaneGeometry::region(PaneNodeId const node_id) const {
  auto const position = node_regions.find(node_id);
  if (position == node_regions.end()) {
    throw std::out_of_range("pane geometry node does not exist");
  }
  return position->second;
}

std::vector<PaneSeparator> const& PaneGeometry::separators() const { return split_separators; }

PaneGeometry calculate_pane_geometry(PaneLayout const& layout, PaneRegion const outer_region) {
  if (outer_region.size.rows < 0 || outer_region.size.cols < 0) {
    throw std::invalid_argument("pane geometry cannot use a negative terminal size");
  }

  PaneGeometry result;
  PaneGeometryCalculator(layout, result).append(layout.root_id(), outer_region);
  return result;
}

}  // namespace moe::parent
