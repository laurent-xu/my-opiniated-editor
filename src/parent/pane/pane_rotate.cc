#include "src/parent/pane/pane_rotate.h"

#include <optional>
#include <stdexcept>
#include <vector>

namespace moe::parent {

namespace {

std::vector<PaneNodeId> child_node_ids(PaneSplit const& split) {
  std::vector<PaneNodeId> result;
  result.reserve(split.children.size());
  for (PaneSplitChild const& child : split.children) {
    result.push_back(child.node_id);
  }
  return result;
}

PaneSelection range_selection(PaneLayout const& layout, std::vector<PaneNodeId> const& nodes,
                              bool const anchor_at_front = true) {
  if (nodes.empty()) {
    throw std::logic_error("pane rotation cannot produce an empty selection");
  }
  if (nodes.size() == 1U) {
    return PaneSelection::single(layout, nodes.front());
  }
  return anchor_at_front ? PaneSelection::range(layout, nodes.front(), nodes.back())
                         : PaneSelection::range(layout, nodes.back(), nodes.front());
}

bool anchor_is_first(PaneSelection const& selection) {
  return selection.anchor() == selection.nodes().front();
}

}  // namespace

bool rotate_pane_selection_level(PaneLayout& layout, PaneSelection& selection) {
  std::vector<PaneNodeId> const selected_nodes = selection.nodes();
  if (selected_nodes.size() > 1U) {
    std::optional<PaneNodeId> const parent = selection.parent();
    if (!parent.has_value()) {
      throw std::logic_error("multi-node pane rotation selection has no parent");
    }
    if (selected_nodes.size() == layout.node(parent.value()).split().children.size()) {
      bool const anchor_at_front = anchor_is_first(selection);
      static_cast<void>(layout.rotate_split(parent.value()));
      selection = range_selection(layout, selected_nodes, anchor_at_front);
      return true;
    }

    PaneNodeId const group = layout.rotate_sibling_range(selected_nodes);
    selection = PaneSelection::single(layout, group);
    return true;
  }

  PaneNodeId const selected = selected_nodes.front();
  if (!layout.node(selected).is_leaf()) {
    std::optional<PaneNodeId> const parent = layout.node(selected).parent();
    std::vector<PaneNodeId> const children = child_node_ids(layout.node(selected).split());
    static_cast<void>(layout.rotate_split(selected));
    selection = parent.has_value() ? range_selection(layout, children)
                                   : PaneSelection::single(layout, selected);
    return true;
  }

  std::optional<PaneNodeId> const parent = selection.parent();
  if (!parent.has_value()) {
    return false;
  }
  std::vector<PaneNodeId> const siblings = child_node_ids(layout.node(parent.value()).split());
  static_cast<void>(layout.rotate_split(parent.value()));
  selection = range_selection(layout, siblings);
  return true;
}

}  // namespace moe::parent
