#include "src/parent/pane/pane_selection.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "src/parent/pane/pane_navigation.h"

namespace moe::parent {

namespace {

std::size_t sibling_index(PaneSplit const& split, PaneNodeId const node_id) {
  auto const position = std::ranges::find_if(
      split.children, [node_id](PaneSplitChild const& child) { return child.node_id == node_id; });
  if (position == split.children.end()) {
    throw std::logic_error("pane selection node is not a child of its parent");
  }
  return static_cast<std::size_t>(position - split.children.begin());
}

}  // namespace

PaneSelection PaneSelection::single(PaneLayout const& layout, PaneNodeId const node_id) {
  PaneLayoutNode const& node = layout.node(node_id);
  return {node.parent(), {.anchor = node_id, .active = node_id}, {node_id}};
}

PaneSelection PaneSelection::range(PaneLayout const& layout, PaneNodeId const anchor,
                                   PaneNodeId const active) {
  std::optional<PaneNodeId> const parent = layout.node(anchor).parent();
  if (layout.node(active).parent() != parent) {
    throw std::invalid_argument("pane selection nodes must have the same direct parent");
  }
  if (!parent.has_value()) {
    if (anchor != active) {
      throw std::invalid_argument("pane selection cannot span separate roots");
    }
    return single(layout, anchor);
  }

  PaneSplit const& siblings = layout.node(parent.value()).split();
  std::size_t const anchor_index = sibling_index(siblings, anchor);
  std::size_t const active_index = sibling_index(siblings, active);
  std::size_t const first_index = std::min(anchor_index, active_index);
  std::size_t const last_index = std::max(anchor_index, active_index);

  std::vector<PaneNodeId> selected_nodes;
  selected_nodes.reserve(last_index - first_index + 1);
  for (std::size_t index = first_index; index <= last_index; ++index) {
    selected_nodes.push_back(siblings.children[index].node_id);
  }
  return {parent, {.anchor = anchor, .active = active}, std::move(selected_nodes)};
}

PaneSelection::PaneSelection(std::optional<PaneNodeId> parent, Endpoints const endpoints,
                             std::vector<PaneNodeId> nodes)
    : parent_id(parent),
      anchor_id(endpoints.anchor),
      active_id(endpoints.active),
      selected_nodes(std::move(nodes)) {}

std::optional<PaneNodeId> PaneSelection::parent() const { return parent_id; }

PaneNodeId PaneSelection::anchor() const { return anchor_id; }

PaneNodeId PaneSelection::active() const { return active_id; }

std::vector<PaneNodeId> const& PaneSelection::nodes() const { return selected_nodes; }

bool PaneSelection::contains(PaneNodeId const node_id) const {
  return std::ranges::find(selected_nodes, node_id) != selected_nodes.end();
}

PaneSelection PaneSelection::step(PaneLayout const& layout,
                                  PaneSiblingDirection const direction) const {
  if (!parent_id.has_value()) {
    return *this;
  }

  PaneSplit const& siblings = layout.node(parent_id.value()).split();
  std::size_t const active_index = sibling_index(siblings, active_id);
  if (direction == PaneSiblingDirection::PREVIOUS) {
    if (active_index == 0) {
      return *this;
    }
    return range(layout, anchor_id, siblings.children[active_index - 1].node_id);
  }
  if (active_index + 1 >= siblings.children.size()) {
    return *this;
  }
  return range(layout, anchor_id, siblings.children[active_index + 1].node_id);
}

PaneSelection PaneSelection::promote(PaneLayout const& layout) const {
  std::optional<PaneNodeId> const parent = find_parent_pane_node(layout, active_id);
  if (!parent.has_value()) {
    return *this;
  }
  return single(layout, parent.value());
}

PaneSelection PaneSelection::descend(PaneLayout const& layout) const {
  if (selected_nodes.size() != 1) {
    return *this;
  }

  std::optional<PaneNodeId> const child =
      find_first_child_pane_node(layout, selected_nodes.front());
  if (!child.has_value()) {
    return *this;
  }
  return single(layout, child.value());
}

}  // namespace moe::parent
