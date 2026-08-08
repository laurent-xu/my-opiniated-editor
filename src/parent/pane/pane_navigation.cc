#include "src/parent/pane/pane_navigation.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <tuple>

namespace moe::parent {
namespace {

struct Interval {
  int begin;
  int end;
};

struct DirectionalScore {
  int primary_gap;
  int perpendicular_gap;
  long long perpendicular_center_distance;

  [[nodiscard]] auto values() const {
    return std::tie(primary_gap, perpendicular_gap, perpendicular_center_distance);
  }
};

Interval horizontal_interval(PaneRegion const& region) {
  return {.begin = region.origin.column, .end = region.origin.column + region.size.cols};
}

Interval vertical_interval(PaneRegion const& region) {
  return {.begin = region.origin.row, .end = region.origin.row + region.size.rows};
}

int interval_gap(Interval const first, Interval const second) {
  if (first.end < second.begin) {
    return second.begin - first.end;
  }
  if (second.end < first.begin) {
    return first.begin - second.end;
  }
  return 0;
}

long long center_distance(Interval const first, Interval const second) {
  long long const first_center_twice =
      static_cast<long long>(first.begin) + static_cast<long long>(first.end);
  long long const second_center_twice =
      static_cast<long long>(second.begin) + static_cast<long long>(second.end);
  return std::abs(first_center_twice - second_center_twice);
}

std::optional<DirectionalScore> score_candidate(PaneRegion const& source,
                                                PaneRegion const& candidate,
                                                PaneFocusDirection const direction) {
  Interval const source_horizontal = horizontal_interval(source);
  Interval const source_vertical = vertical_interval(source);
  Interval const candidate_horizontal = horizontal_interval(candidate);
  Interval const candidate_vertical = vertical_interval(candidate);

  switch (direction) {
    case PaneFocusDirection::LEFT:
      if (candidate_horizontal.end > source_horizontal.begin) {
        return std::nullopt;
      }
      return DirectionalScore{
          .primary_gap = source_horizontal.begin - candidate_horizontal.end,
          .perpendicular_gap = interval_gap(source_vertical, candidate_vertical),
          .perpendicular_center_distance = center_distance(source_vertical, candidate_vertical),
      };
    case PaneFocusDirection::RIGHT:
      if (candidate_horizontal.begin < source_horizontal.end) {
        return std::nullopt;
      }
      return DirectionalScore{
          .primary_gap = candidate_horizontal.begin - source_horizontal.end,
          .perpendicular_gap = interval_gap(source_vertical, candidate_vertical),
          .perpendicular_center_distance = center_distance(source_vertical, candidate_vertical),
      };
    case PaneFocusDirection::UP:
      if (candidate_vertical.end > source_vertical.begin) {
        return std::nullopt;
      }
      return DirectionalScore{
          .primary_gap = source_vertical.begin - candidate_vertical.end,
          .perpendicular_gap = interval_gap(source_horizontal, candidate_horizontal),
          .perpendicular_center_distance = center_distance(source_horizontal, candidate_horizontal),
      };
    case PaneFocusDirection::DOWN:
      if (candidate_vertical.begin < source_vertical.end) {
        return std::nullopt;
      }
      return DirectionalScore{
          .primary_gap = candidate_vertical.begin - source_vertical.end,
          .perpendicular_gap = interval_gap(source_horizontal, candidate_horizontal),
          .perpendicular_center_distance = center_distance(source_horizontal, candidate_horizontal),
      };
  }
  throw std::logic_error("unknown pane focus direction");
}

std::optional<PaneNodeId> find_immediate_directional_pane(PaneLayout const& layout,
                                                          PaneGeometry const& geometry,
                                                          PaneNodeId const source,
                                                          PaneFocusDirection const direction) {
  PaneRegion const& source_region = geometry.region(source);
  std::optional<PaneNodeId> best_node;
  std::optional<DirectionalScore> best_score;
  for (PaneNodeId const candidate : layout.leaf_nodes()) {
    if (candidate == source) {
      continue;
    }
    PaneRegion const& candidate_region = geometry.region(candidate);
    if (candidate_region.size.rows <= 0 || candidate_region.size.cols <= 0) {
      continue;
    }
    std::optional<DirectionalScore> const score =
        score_candidate(source_region, candidate_region, direction);
    if (!score.has_value()) {
      continue;
    }
    if (!best_score.has_value() || score->values() < best_score->values()) {
      best_node = candidate;
      best_score = score;
    }
  }
  return best_node;
}

}  // namespace

std::optional<PaneNodeId> find_directional_pane(PaneLayout const& layout,
                                                PaneGeometry const& geometry,
                                                PaneNodeId const source,
                                                PaneFocusDirection const direction) {
  return find_immediate_directional_pane(layout, geometry, source, direction);
}

std::optional<PaneNodeId> find_directional_pane(PaneLayout const& layout,
                                                PaneGeometry const& geometry,
                                                PaneNodeId const source,
                                                PaneFocusDirection const direction,
                                                PaneNodeEligibility const& eligible) {
  PaneNodeId cursor = source;
  std::size_t remaining = layout.leaf_nodes().size();
  while (remaining > 0U) {
    std::optional<PaneNodeId> const candidate =
        find_immediate_directional_pane(layout, geometry, cursor, direction);
    if (!candidate.has_value()) {
      return std::nullopt;
    }
    cursor = candidate.value();
    if (eligible(cursor)) {
      return cursor;
    }
    --remaining;
  }
  return std::nullopt;
}

std::optional<PaneNodeId> find_parent_pane_node(PaneLayout const& layout, PaneNodeId const source) {
  return layout.node(source).parent();
}

std::optional<PaneNodeId> find_parent_pane_node(PaneLayout const& layout, PaneNodeId const source,
                                                PaneNodeEligibility const& eligible) {
  std::optional<PaneNodeId> const parent = find_parent_pane_node(layout, source);
  return parent.has_value() && eligible(parent.value()) ? parent : std::nullopt;
}

std::optional<PaneNodeId> find_first_child_pane_node(PaneLayout const& layout,
                                                     PaneNodeId const source) {
  PaneLayoutNode const& node = layout.node(source);
  return node.is_leaf() ? std::nullopt
                        : std::optional<PaneNodeId>(node.split().children.front().node_id);
}

std::optional<PaneNodeId> find_first_child_pane_node(PaneLayout const& layout,
                                                     PaneNodeId const source,
                                                     PaneNodeEligibility const& eligible) {
  PaneLayoutNode const& node = layout.node(source);
  if (node.is_leaf()) {
    return std::nullopt;
  }
  auto const child = std::ranges::find_if(
      node.split().children,
      [&eligible](PaneSplitChild const& split_child) { return eligible(split_child.node_id); });
  return child == node.split().children.end() ? std::nullopt
                                              : std::optional<PaneNodeId>(child->node_id);
}

}  // namespace moe::parent
