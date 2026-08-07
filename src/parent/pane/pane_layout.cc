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

std::optional<PaneNodeId> PaneLayout::remove_leaf(PaneNodeId const target) {
  PaneLayoutNode const& target_node = node(target);
  if (!target_node.is_leaf()) {
    throw std::invalid_argument("only leaf pane nodes can be removed");
  }

  std::vector<PaneNodeId> const leaves = leaf_nodes();
  if (leaves.size() == 1) {
    return std::nullopt;
  }
  auto const target_position = std::ranges::find(leaves, target);
  if (target_position == leaves.end()) {
    throw std::logic_error("pane leaf is not reachable from the layout root");
  }
  std::size_t const target_leaf_index = static_cast<std::size_t>(target_position - leaves.begin());
  PaneNodeId const next_focus = target_leaf_index + 1 < leaves.size()
                                    ? leaves[target_leaf_index + 1]
                                    : leaves[target_leaf_index - 1];

  std::optional<PaneNodeId> const parent_id = target_node.parent();
  if (!parent_id.has_value()) {
    throw std::logic_error("multi-pane layout leaf has no parent");
  }
  PaneSplit& parent_split = nodes.at(parent_id.value()).mutable_split();
  std::size_t const target_child_index = child_index(parent_split, target);
  auto const child_position =
      parent_split.children.begin() + static_cast<std::ptrdiff_t>(target_child_index);
  parent_split.children.erase(child_position);

  std::vector<int> remaining_weights;
  remaining_weights.reserve(parent_split.children.size());
  for (PaneSplitChild const& child : parent_split.children) {
    remaining_weights.push_back(child.percentage.value());
  }
  std::vector<PanePercentage> const normalized = normalize_pane_percentages(remaining_weights);
  for (std::size_t index = 0; index < parent_split.children.size(); ++index) {
    parent_split.children[index].percentage = normalized[index];
  }
  nodes.erase(target);

  if (parent_split.children.size() == 1) {
    dissolve_unary_split(parent_id.value());
  }
  return next_focus;
}

bool PaneLayout::rotate_split(PaneNodeId const split_node) {
  if (node(split_node).is_leaf()) {
    return false;
  }
  std::optional<PaneNodeId> const parent = node(split_node).parent();
  toggle_split_axes(split_node);
  if (parent.has_value() && node(parent.value()).split().axis == node(split_node).split().axis) {
    flatten_matching_split_children(parent.value());
  }
  return true;
}

PaneNodeId PaneLayout::rotate_sibling_range(std::vector<PaneNodeId> const& siblings) {
  if (siblings.size() < 2U) {
    throw std::invalid_argument("pane rotation range requires at least two siblings");
  }
  std::optional<PaneNodeId> const parent = node(siblings.front()).parent();
  if (!parent.has_value()) {
    throw std::invalid_argument("pane rotation range must have a parent");
  }

  PaneSplit& parent_split = nodes.at(parent.value()).mutable_split();
  std::size_t const first_index = child_index(parent_split, siblings.front());
  if (first_index + siblings.size() > parent_split.children.size()) {
    throw std::invalid_argument("pane rotation range must be contiguous siblings");
  }
  for (std::size_t offset = 0; offset < siblings.size(); ++offset) {
    if (parent_split.children[first_index + offset].node_id != siblings[offset]) {
      throw std::invalid_argument("pane rotation range must be contiguous siblings");
    }
  }
  if (siblings.size() == parent_split.children.size()) {
    throw std::invalid_argument("complete pane rotation levels must rotate their parent split");
  }

  int selected_share = 0;
  std::vector<int> selected_weights;
  selected_weights.reserve(siblings.size());
  for (std::size_t offset = 0; offset < siblings.size(); ++offset) {
    int const weight = parent_split.children[first_index + offset].percentage.value();
    selected_share += weight;
    selected_weights.push_back(weight);
  }

  std::vector<PanePercentage> const selected_percentages =
      normalize_pane_percentages(selected_weights);
  std::vector<PaneSplitChild> selected_children;
  selected_children.reserve(siblings.size());
  for (std::size_t offset = 0; offset < siblings.size(); ++offset) {
    selected_children.push_back(
        {.node_id = siblings[offset], .percentage = selected_percentages[offset]});
  }

  PaneNodeId const group_id = allocate_node_id();
  PaneSplit group{
      .axis = parent_split.axis == PaneSplitAxis::LEFT_TO_RIGHT ? PaneSplitAxis::TOP_TO_BOTTOM
                                                                : PaneSplitAxis::LEFT_TO_RIGHT,
      .children = std::move(selected_children),
  };

  auto const first_child = parent_split.children.begin() + static_cast<std::ptrdiff_t>(first_index);
  parent_split.children.erase(first_child,
                              first_child + static_cast<std::ptrdiff_t>(siblings.size()));
  parent_split.children.insert(
      parent_split.children.begin() + static_cast<std::ptrdiff_t>(first_index),
      {.node_id = group_id, .percentage = required_percentage(selected_share)});

  for (PaneNodeId const sibling : siblings) {
    nodes.at(sibling).set_parent(group_id);
    toggle_split_axes(sibling);
  }
  nodes.emplace(group_id, PaneLayoutNode(group_id, parent, std::move(group)));
  return group_id;
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

void PaneLayout::toggle_split_axes(PaneNodeId const node_id) {
  PaneLayoutNode& layout_node = nodes.at(node_id);
  if (layout_node.is_leaf()) {
    return;
  }

  PaneSplit& split = layout_node.mutable_split();
  split.axis = split.axis == PaneSplitAxis::LEFT_TO_RIGHT ? PaneSplitAxis::TOP_TO_BOTTOM
                                                          : PaneSplitAxis::LEFT_TO_RIGHT;
  std::vector<PaneNodeId> children;
  children.reserve(split.children.size());
  for (PaneSplitChild const& child : split.children) {
    children.push_back(child.node_id);
  }
  for (PaneNodeId const child : children) {
    toggle_split_axes(child);
  }
}

void PaneLayout::dissolve_unary_split(PaneNodeId const split_node) {
  PaneLayoutNode const& unary_node = node(split_node);
  PaneSplit const& unary_split = unary_node.split();
  if (unary_split.children.size() != 1) {
    throw std::logic_error("only a unary pane split can be dissolved");
  }

  PaneNodeId const child_id = unary_split.children.front().node_id;
  std::optional<PaneNodeId> const grandparent_id = unary_node.parent();
  if (!grandparent_id.has_value()) {
    root = child_id;
    nodes.at(child_id).set_parent(std::nullopt);
    nodes.erase(split_node);
    return;
  }

  PaneSplit& grandparent_split = nodes.at(grandparent_id.value()).mutable_split();
  std::size_t const inherited_index = child_index(grandparent_split, split_node);
  PaneLayoutNode const& child_node = node(child_id);
  if (!child_node.is_leaf() && child_node.split().axis == grandparent_split.axis) {
    PaneSplit const promoted_split = child_node.split();
    int const inherited_percentage = grandparent_split.children[inherited_index].percentage.value();
    std::vector<int> promoted_weights;
    promoted_weights.reserve(promoted_split.children.size());
    for (PaneSplitChild const& promoted_child : promoted_split.children) {
      promoted_weights.push_back(promoted_child.percentage.value());
    }
    std::vector<int> const inherited_percentages =
        distribute_pane_percentage_total(promoted_weights, inherited_percentage);
    std::vector<PaneSplitChild> promoted_children = promoted_split.children;
    for (std::size_t index = 0; index < promoted_children.size(); ++index) {
      promoted_children[index].percentage = required_percentage(inherited_percentages[index]);
    }

    auto const inherited_child_position =
        grandparent_split.children.begin() + static_cast<std::ptrdiff_t>(inherited_index);
    grandparent_split.children.erase(inherited_child_position);
    grandparent_split.children.insert(
        grandparent_split.children.begin() + static_cast<std::ptrdiff_t>(inherited_index),
        promoted_children.begin(), promoted_children.end());

    for (PaneSplitChild const& promoted_child : promoted_split.children) {
      nodes.at(promoted_child.node_id).set_parent(grandparent_id);
    }
    nodes.erase(child_id);
    nodes.erase(split_node);
    return;
  }

  grandparent_split.children[inherited_index].node_id = child_id;
  nodes.at(child_id).set_parent(grandparent_id);
  nodes.erase(split_node);
}

void PaneLayout::flatten_matching_split_children(PaneNodeId const split_node) {
  PaneSplit& split = nodes.at(split_node).mutable_split();
  std::vector<PaneSplitChild> children;
  std::vector<PaneNodeId> flattened_nodes;
  for (PaneSplitChild const& split_child : split.children) {
    PaneNodeId const child_id = split_child.node_id;
    PaneLayoutNode const& child = node(child_id);
    if (child.is_leaf() || child.split().axis != split.axis) {
      children.push_back(split_child);
      continue;
    }

    PaneSplit const child_split = child.split();
    std::vector<int> child_weights;
    child_weights.reserve(child_split.children.size());
    for (PaneSplitChild const& nested_child : child_split.children) {
      child_weights.push_back(nested_child.percentage.value());
    }
    std::vector<int> const distributed =
        distribute_pane_percentage_total(child_weights, split_child.percentage.value());
    std::vector<PaneSplitChild> promoted_children = child_split.children;
    for (std::size_t index = 0; index < promoted_children.size(); ++index) {
      promoted_children[index].percentage = required_percentage(distributed[index]);
    }
    children.insert(children.end(), promoted_children.begin(), promoted_children.end());
    flattened_nodes.push_back(child_id);
  }
  split.children = std::move(children);
  for (PaneSplitChild const& child : split.children) {
    nodes.at(child.node_id).set_parent(split_node);
  }
  for (PaneNodeId const flattened : flattened_nodes) {
    nodes.erase(flattened);
  }
}

}  // namespace moe::parent
