#include "src/parent/pane/pane_layout.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moe::parent {

namespace {

PaneNodeId required_node_id(PaneNodeId::Value const value) {
  std::optional<PaneNodeId> const node_id = PaneNodeId::from_value(value);
  if (!node_id.has_value()) {
    throw std::logic_error("pane node id cannot be zero");
  }
  return node_id.value();
}

PanePercentage required_percentage(int const value) {
  std::optional<PanePercentage> const percentage = PanePercentage::from_int(value);
  if (!percentage.has_value()) {
    throw std::logic_error("pane percentage is outside 0..100");
  }
  return percentage.value();
}

std::size_t child_index(PaneSplit const& split, PaneNodeId const child) {
  auto const position = std::ranges::find_if(
      split.children,
      [child](PaneSplitChild const& split_child) { return split_child.node_id == child; });
  if (position == split.children.end()) {
    throw std::logic_error("pane split does not contain its child");
  }
  return static_cast<std::size_t>(position - split.children.begin());
}

}  // namespace

PaneLayoutNode::PaneLayoutNode(PaneNodeId const id, std::optional<PaneNodeId> parent,
                               PaneId const pane_id)
    : node_id(id), parent_id(parent), contents(pane_id) {}

PaneLayoutNode::PaneLayoutNode(PaneNodeId const id, std::optional<PaneNodeId> parent,
                               PaneSplit split)
    : node_id(id), parent_id(parent), contents(std::move(split)) {}

PaneNodeId PaneLayoutNode::id() const { return node_id; }

std::optional<PaneNodeId> PaneLayoutNode::parent() const { return parent_id; }

bool PaneLayoutNode::is_leaf() const { return std::holds_alternative<PaneId>(contents); }

PaneId PaneLayoutNode::pane_id() const {
  if (!is_leaf()) {
    throw std::logic_error("split pane node has no pane id");
  }
  return std::get<PaneId>(contents);
}

PaneSplit const& PaneLayoutNode::split() const {
  if (is_leaf()) {
    throw std::logic_error("leaf pane node has no split");
  }
  return std::get<PaneSplit>(contents);
}

PaneSplit& PaneLayoutNode::mutable_split() {
  if (is_leaf()) {
    throw std::logic_error("leaf pane node has no split");
  }
  return std::get<PaneSplit>(contents);
}

void PaneLayoutNode::set_parent(std::optional<PaneNodeId> parent) { parent_id = parent; }

PaneLayout PaneLayout::single(PaneId const pane_id) {
  PaneNodeId const root = required_node_id(1);
  std::map<PaneNodeId, PaneLayoutNode> nodes;
  nodes.emplace(root, PaneLayoutNode(root, std::nullopt, pane_id));
  return {root, std::move(nodes), 2};
}

PaneLayout::PaneLayout(PaneNodeId const root_node,
                       std::map<PaneNodeId, PaneLayoutNode> layout_nodes,
                       PaneNodeId::Value const next_value)
    : root(root_node), nodes(std::move(layout_nodes)), next_node_value(next_value) {}

PaneNodeId PaneLayout::root_id() const { return root; }

PaneLayoutNode const& PaneLayout::node(PaneNodeId const node_id) const {
  auto const position = nodes.find(node_id);
  if (position == nodes.end()) {
    throw std::out_of_range("pane layout node does not exist");
  }
  return position->second;
}

std::optional<PaneNodeId> PaneLayout::find_pane(PaneId const pane_id) const {
  for (auto const& [node_id, layout_node] : nodes) {
    if (layout_node.is_leaf() && layout_node.pane_id() == pane_id) {
      return node_id;
    }
  }
  return std::nullopt;
}

std::vector<PaneNodeId> PaneLayout::leaf_nodes() const {
  std::vector<PaneNodeId> result;
  append_leaf_nodes(root, result);
  return result;
}

PaneNodeId PaneLayout::split_leaf(PaneNodeId const target, PaneSplitAxis const axis,
                                  PaneId const new_pane, PaneInsertion const insertion) {
  PaneLayoutNode& target_node = nodes.at(target);
  if (!target_node.is_leaf()) {
    throw std::invalid_argument("only leaf pane nodes can be split");
  }
  if (find_pane(new_pane).has_value()) {
    throw std::invalid_argument("pane id already exists in layout");
  }

  PaneNodeId const new_leaf_id = allocate_node_id();
  std::optional<PaneNodeId> const parent_id = target_node.parent();
  if (parent_id.has_value() && node(parent_id.value()).split().axis == axis) {
    PaneSplit& parent_split = nodes.at(parent_id.value()).mutable_split();
    std::size_t const target_index = child_index(parent_split, target);
    int const target_percentage = parent_split.children[target_index].percentage.value();
    int const first_percentage = (target_percentage + 1) / 2;
    int const second_percentage = target_percentage / 2;
    std::size_t const new_index =
        insertion == PaneInsertion::BEFORE ? target_index : target_index + 1;

    auto const child_position =
        parent_split.children.begin() + static_cast<std::ptrdiff_t>(new_index);
    parent_split.children[target_index].percentage = required_percentage(
        insertion == PaneInsertion::BEFORE ? second_percentage : first_percentage);
    parent_split.children.insert(
        child_position,
        PaneSplitChild{
            .node_id = new_leaf_id,
            .percentage = required_percentage(
                insertion == PaneInsertion::BEFORE ? first_percentage : second_percentage),
        });
    nodes.emplace(new_leaf_id, PaneLayoutNode(new_leaf_id, parent_id, new_pane));
    return new_leaf_id;
  }

  PaneNodeId const split_id = allocate_node_id();
  PanePercentage const half = required_percentage(50);
  std::vector<PaneSplitChild> children;
  if (insertion == PaneInsertion::BEFORE) {
    children = {{.node_id = new_leaf_id, .percentage = half},
                {.node_id = target, .percentage = half}};
  } else {
    children = {{.node_id = target, .percentage = half},
                {.node_id = new_leaf_id, .percentage = half}};
  }
  PaneSplit split{
      .axis = axis,
      .children = std::move(children),
  };

  if (parent_id.has_value()) {
    PaneSplit& parent_split = nodes.at(parent_id.value()).mutable_split();
    parent_split.children[child_index(parent_split, target)].node_id = split_id;
  } else {
    root = split_id;
  }

  target_node.set_parent(split_id);
  nodes.emplace(new_leaf_id, PaneLayoutNode(new_leaf_id, split_id, new_pane));
  nodes.emplace(split_id, PaneLayoutNode(split_id, parent_id, std::move(split)));
  return new_leaf_id;
}

void PaneLayout::set_split_percentages(PaneNodeId const split_node,
                                       std::vector<int> const& weights) {
  PaneSplit& split = nodes.at(split_node).mutable_split();
  if (weights.size() != split.children.size()) {
    throw std::invalid_argument("pane percentage count must match split child count");
  }
  std::vector<PanePercentage> const percentages = normalize_pane_percentages(weights);
  for (std::size_t index = 0; index < split.children.size(); ++index) {
    split.children[index].percentage = percentages[index];
  }
}

PaneNodeId PaneLayout::allocate_node_id() {
  if (next_node_value == 0 || next_node_value == std::numeric_limits<PaneNodeId::Value>::max()) {
    throw std::overflow_error("pane layout exhausted node ids");
  }
  PaneNodeId const result = required_node_id(next_node_value);
  ++next_node_value;
  return result;
}

void PaneLayout::append_leaf_nodes(PaneNodeId const node_id,
                                   std::vector<PaneNodeId>& output) const {
  PaneLayoutNode const& layout_node = node(node_id);
  if (layout_node.is_leaf()) {
    output.push_back(node_id);
    return;
  }
  for (PaneSplitChild const& child : layout_node.split().children) {
    append_leaf_nodes(child.node_id, output);
  }
}

}  // namespace moe::parent
