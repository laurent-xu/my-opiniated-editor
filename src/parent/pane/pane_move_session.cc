#include "src/parent/pane/pane_move_session.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace moe::parent {
namespace {

bool is_ancestor(PaneLayout const& layout, PaneNodeId const ancestor, PaneNodeId descendant) {
  std::optional<PaneNodeId> parent = layout.node(descendant).parent();
  while (parent.has_value()) {
    if (parent.value() == ancestor) {
      return true;
    }
    parent = layout.node(parent.value()).parent();
  }
  return false;
}

}  // namespace

PaneMoveSession PaneMoveSession::begin(PaneSelection source) {
  return PaneMoveSession(std::move(source));
}

PaneMoveSession::PaneMoveSession(PaneSelection source) : source_selection(std::move(source)) {}

PaneSelection const& PaneMoveSession::source() const { return source_selection; }

std::optional<PaneNodeId> PaneMoveSession::target() const { return target_node; }

PaneMoveStage PaneMoveSession::stage() const { return move_stage; }

PaneMoveOperation PaneMoveSession::operation() const { return move_operation; }

std::optional<PaneDropDirection> PaneMoveSession::drop_direction() const {
  return selected_drop_direction;
}

bool PaneMoveSession::step_target(PaneLayout const& layout, PaneGeometry const& geometry,
                                  PaneFocusDirection const direction) {
  if (move_stage != PaneMoveStage::TARGET) {
    return false;
  }
  PaneNodeId cursor = target_node.value_or(source_selection.active());
  std::size_t remaining = layout.leaf_nodes().size();
  while (remaining > 0U) {
    std::optional<PaneNodeId> const candidate =
        find_directional_pane(layout, geometry, cursor, direction);
    if (!candidate.has_value()) {
      return false;
    }
    cursor = candidate.value();
    if (target_is_eligible(layout, cursor)) {
      target_node = cursor;
      return true;
    }
    --remaining;
  }
  return false;
}

bool PaneMoveSession::promote_target(PaneLayout const& layout) {
  if (move_stage != PaneMoveStage::TARGET || !target_node.has_value()) {
    return false;
  }
  std::optional<PaneNodeId> const parent = layout.node(target_node.value()).parent();
  if (!parent.has_value() || !target_is_eligible(layout, parent.value())) {
    return false;
  }
  target_node = parent;
  return true;
}

bool PaneMoveSession::descend_target(PaneLayout const& layout) {
  if (move_stage != PaneMoveStage::TARGET || !target_node.has_value()) {
    return false;
  }
  PaneLayoutNode const& target = layout.node(target_node.value());
  if (target.is_leaf()) {
    return false;
  }
  auto const child = std::ranges::find_if(target.split().children,
                                          [this, &layout](PaneSplitChild const& split_child) {
                                            return target_is_eligible(layout, split_child.node_id);
                                          });
  if (child == target.split().children.end()) {
    return false;
  }
  target_node = child->node_id;
  return true;
}

bool PaneMoveSession::toggle_swap() {
  if (move_stage != PaneMoveStage::TARGET || source_selection.nodes().size() != 1U) {
    return false;
  }
  move_operation =
      move_operation == PaneMoveOperation::MOVE ? PaneMoveOperation::SWAP : PaneMoveOperation::MOVE;
  return true;
}

bool PaneMoveSession::lock_target() {
  if (move_stage != PaneMoveStage::TARGET || move_operation != PaneMoveOperation::MOVE ||
      !target_node.has_value()) {
    return false;
  }
  move_stage = PaneMoveStage::DROP;
  return true;
}

bool PaneMoveSession::set_drop_direction(PaneDropDirection const direction) {
  if (move_stage != PaneMoveStage::DROP) {
    return false;
  }
  bool const changed = selected_drop_direction != direction;
  selected_drop_direction = direction;
  return changed;
}

std::optional<PaneLayout> PaneMoveSession::preview(PaneLayout const& layout) const {
  if (!target_node.has_value()) {
    return std::nullopt;
  }
  PaneLayout result = layout;
  bool changed = false;
  if (move_operation == PaneMoveOperation::SWAP) {
    changed = swap_pane_nodes(result, source_selection.nodes().front(), target_node.value());
  } else if (move_stage == PaneMoveStage::DROP && selected_drop_direction.has_value()) {
    changed = move_pane_selection(result, source_selection, target_node.value(),
                                  selected_drop_direction.value());
  }
  return changed ? std::optional<PaneLayout>(std::move(result)) : std::nullopt;
}

bool PaneMoveSession::confirm(PaneLayout& layout) const {
  std::optional<PaneLayout> candidate = preview(layout);
  if (!candidate.has_value()) {
    return false;
  }
  layout = std::move(candidate.value());
  return true;
}

bool PaneMoveSession::target_is_eligible(PaneLayout const& layout,
                                         PaneNodeId const candidate) const {
  return std::ranges::none_of(
      source_selection.nodes(), [&layout, candidate](PaneNodeId const source) {
        return source == candidate || is_ancestor(layout, source, candidate) ||
               is_ancestor(layout, candidate, source);
      });
}

}  // namespace moe::parent
