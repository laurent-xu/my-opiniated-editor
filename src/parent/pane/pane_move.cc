#include "src/parent/pane/pane_move.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace moe::parent {
namespace {

struct DropPlacement {
  PaneSplitAxis axis;
  PaneInsertion insertion;
};

struct ExtractedSource {
  PaneNodeId node_id;
  PaneNodeId cleanup_parent;
};

struct ParentChild {
  PaneNodeId parent;
  PaneNodeId child;
};

struct NodeRelocation {
  PaneNodeId source;
  PaneNodeId target;
};

DropPlacement placement_for(PaneDropDirection const direction) {
  switch (direction) {
    case PaneDropDirection::LEFT:
      return {.axis = PaneSplitAxis::LEFT_TO_RIGHT, .insertion = PaneInsertion::BEFORE};
    case PaneDropDirection::RIGHT:
      return {.axis = PaneSplitAxis::LEFT_TO_RIGHT, .insertion = PaneInsertion::AFTER};
    case PaneDropDirection::UP:
      return {.axis = PaneSplitAxis::TOP_TO_BOTTOM, .insertion = PaneInsertion::BEFORE};
    case PaneDropDirection::DOWN:
      return {.axis = PaneSplitAxis::TOP_TO_BOTTOM, .insertion = PaneInsertion::AFTER};
  }
  throw std::logic_error("unknown pane drop direction");
}

std::vector<int> percentage_values(std::vector<PaneSplitChild> const& children) {
  std::vector<int> result;
  result.reserve(children.size());
  for (PaneSplitChild const& child : children) {
    result.push_back(child.percentage.value());
  }
  return result;
}

PanePercentage required_percentage(int const value) {
  std::optional<PanePercentage> const percentage = PanePercentage::from_int(value);
  if (!percentage.has_value()) {
    throw std::logic_error("pane move produced an invalid percentage");
  }
  return percentage.value();
}

void assign_percentages(std::vector<PaneSplitChild>& children,
                        std::vector<int> const& percentages) {
  if (children.size() != percentages.size()) {
    throw std::logic_error("pane move child and percentage counts differ");
  }
  for (std::size_t index = 0; index < children.size(); ++index) {
    children[index].percentage = required_percentage(percentages[index]);
  }
}

void normalize_percentages(std::vector<PaneSplitChild>& children) {
  std::vector<PanePercentage> const normalized =
      normalize_pane_percentages(percentage_values(children));
  for (std::size_t index = 0; index < children.size(); ++index) {
    children[index].percentage = normalized[index];
  }
}

std::size_t child_index(PaneSplit const& split, PaneNodeId const node_id) {
  auto const position = std::ranges::find_if(
      split.children, [node_id](PaneSplitChild const& child) { return child.node_id == node_id; });
  if (position == split.children.end()) {
    throw std::logic_error("pane split does not contain child");
  }
  return static_cast<std::size_t>(position - split.children.begin());
}

PaneNodeId required_selection_parent(PaneSelection const& selection) {
  std::optional<PaneNodeId> const parent = selection.parent();
  if (!parent.has_value()) {
    throw std::invalid_argument("pane layout root cannot be moved");
  }
  return parent.value();
}

}  // namespace

class PaneLayoutMover {
 public:
  explicit PaneLayoutMover(PaneLayout& destination) : layout(destination) {}

  [[nodiscard]] bool move(PaneSelection const& selection, PaneNodeId const target,
                          PaneDropDirection const direction) {
    validate(selection, target);
    DropPlacement const placement = placement_for(direction);
    PaneNodeId const source_parent = required_selection_parent(selection);
    PaneSplit const& parent_split = layout.node(source_parent).split();
    if (layout.node(target).parent() == selection.parent() && parent_split.axis == placement.axis) {
      return reorder_at_same_level(selection, target, placement.insertion);
    }

    ExtractedSource const source = extract(selection);
    insert_at_target({.source = source.node_id, .target = target}, placement);
    PaneSplit const& cleanup = layout.node(source.cleanup_parent).split();
    if (cleanup.children.size() == 1U) {
      layout.dissolve_unary_split(source.cleanup_parent);
    }
    return true;
  }

  [[nodiscard]] bool exchange_nodes(PaneNodeId const source, PaneNodeId const target) {
    if (source == target) {
      return false;
    }
    static_cast<void>(layout.node(source));
    static_cast<void>(layout.node(target));
    if (is_ancestor(source, target) || is_ancestor(target, source)) {
      throw std::invalid_argument("pane swap source and target cannot overlap by ancestry");
    }

    std::optional<PaneNodeId> const source_parent = layout.node(source).parent();
    std::optional<PaneNodeId> const target_parent = layout.node(target).parent();
    if (!source_parent.has_value() || !target_parent.has_value()) {
      throw std::invalid_argument("pane layout root cannot be swapped");
    }
    PaneSplit& source_split = layout.nodes.at(source_parent.value()).mutable_split();
    PaneSplit& target_split = layout.nodes.at(target_parent.value()).mutable_split();
    std::size_t const source_index = child_index(source_split, source);
    std::size_t const target_index = child_index(target_split, target);
    source_split.children[source_index].node_id = target;
    target_split.children[target_index].node_id = source;
    layout.nodes.at(source).set_parent(target_parent);
    layout.nodes.at(target).set_parent(source_parent);
    normalize_direct_children(source_parent.value());
    if (target_parent != source_parent) {
      normalize_direct_children(target_parent.value());
    }
    return true;
  }

 private:
  void validate(PaneSelection const& selection, PaneNodeId const target) const {
    PaneNodeId const source_parent = required_selection_parent(selection);
    PaneSplit const& siblings = layout.node(source_parent).split();
    std::vector<PaneNodeId> const& selected = selection.nodes();
    if (selected.empty()) {
      throw std::invalid_argument("pane move requires a source node");
    }
    std::size_t const first_index = child_index(siblings, selected.front());
    if (first_index + selected.size() > siblings.children.size()) {
      throw std::invalid_argument("pane move selection must be a contiguous sibling range");
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
      if (siblings.children[first_index + index].node_id != selected[index]) {
        throw std::invalid_argument("pane move selection must be a contiguous sibling range");
      }
    }

    static_cast<void>(layout.node(target));
    for (PaneNodeId const source : selected) {
      if (source == target || is_ancestor(source, target) || is_ancestor(target, source)) {
        throw std::invalid_argument("pane move source and target cannot overlap by ancestry");
      }
    }
    if (selected.size() == siblings.children.size() && source_parent == layout.root) {
      throw std::invalid_argument("entire pane layout cannot be moved");
    }
  }

  [[nodiscard]] bool is_ancestor(PaneNodeId const ancestor, PaneNodeId descendant) const {
    std::optional<PaneNodeId> parent = layout.node(descendant).parent();
    while (parent.has_value()) {
      if (parent.value() == ancestor) {
        return true;
      }
      parent = layout.node(parent.value()).parent();
    }
    return false;
  }

  [[nodiscard]] bool reorder_at_same_level(PaneSelection const& selection, PaneNodeId const target,
                                           PaneInsertion const insertion) {
    PaneNodeId const parent_id = required_selection_parent(selection);
    PaneSplit& split = layout.nodes.at(parent_id).mutable_split();
    std::vector<PaneSplitChild> const original_children = split.children;
    std::vector<PaneSplitChild> moved_children;
    std::vector<PaneSplitChild> remaining_children;
    for (PaneSplitChild const& child : original_children) {
      std::vector<PaneSplitChild>& destination =
          selection.contains(child.node_id) ? moved_children : remaining_children;
      destination.push_back(child);
    }

    auto const target_position = std::ranges::find_if(
        remaining_children,
        [target](PaneSplitChild const& child) { return child.node_id == target; });
    if (target_position == remaining_children.end()) {
      throw std::logic_error("pane move target disappeared during same-level reorder");
    }
    std::size_t target_index =
        static_cast<std::size_t>(target_position - remaining_children.begin());
    if (insertion == PaneInsertion::AFTER) {
      ++target_index;
    }
    remaining_children.insert(
        remaining_children.begin() + static_cast<std::ptrdiff_t>(target_index),
        moved_children.begin(), moved_children.end());
    bool const unchanged =
        std::ranges::equal(remaining_children, original_children,
                           [](PaneSplitChild const& lhs, PaneSplitChild const& rhs) {
                             return lhs.node_id == rhs.node_id;
                           });
    if (unchanged) {
      return false;
    }
    split.children = std::move(remaining_children);
    return true;
  }

  [[nodiscard]] ExtractedSource extract(PaneSelection const& selection) {
    PaneNodeId const parent_id = required_selection_parent(selection);
    PaneSplit& split = layout.nodes.at(parent_id).mutable_split();
    if (selection.nodes().size() == split.children.size()) {
      std::optional<PaneNodeId> const grandparent = layout.node(parent_id).parent();
      if (!grandparent.has_value()) {
        throw std::logic_error("validated move unexpectedly tried to extract its root");
      }
      detach_child({.parent = grandparent.value(), .child = parent_id});
      layout.nodes.at(parent_id).set_parent(std::nullopt);
      return {.node_id = parent_id, .cleanup_parent = grandparent.value()};
    }

    std::vector<PaneSplitChild> selected_children;
    std::vector<PaneSplitChild> remaining_children;
    for (PaneSplitChild const& child : split.children) {
      if (selection.contains(child.node_id)) {
        selected_children.push_back(child);
      } else {
        remaining_children.push_back(child);
      }
    }
    if (selected_children.size() != selection.nodes().size()) {
      throw std::logic_error("validated pane move selection changed during extraction");
    }
    normalize_percentages(remaining_children);
    split.children = std::move(remaining_children);

    if (selected_children.size() == 1U) {
      PaneNodeId const source = selected_children.front().node_id;
      layout.nodes.at(source).set_parent(std::nullopt);
      return {.node_id = source, .cleanup_parent = parent_id};
    }

    PaneNodeId const group_id = layout.allocate_node_id();
    normalize_percentages(selected_children);
    PaneSplit group{
        .axis = split.axis,
        .children = std::move(selected_children),
    };
    for (PaneSplitChild const& child : group.children) {
      layout.nodes.at(child.node_id).set_parent(group_id);
    }
    layout.nodes.emplace(group_id, PaneLayoutNode(group_id, std::nullopt, std::move(group)));
    return {.node_id = group_id, .cleanup_parent = parent_id};
  }

  void detach_child(ParentChild const nodes) {
    PaneSplit& split = layout.nodes.at(nodes.parent).mutable_split();
    auto const position = std::ranges::find_if(
        split.children,
        [nodes](PaneSplitChild const& child) { return child.node_id == nodes.child; });
    if (position == split.children.end()) {
      throw std::logic_error("pane move source parent does not contain source node");
    }
    split.children.erase(position);
    normalize_percentages(split.children);
  }

  void insert_at_target(NodeRelocation const nodes, DropPlacement const placement) {
    std::optional<PaneNodeId> const target_parent = layout.node(nodes.target).parent();
    if (target_parent.has_value() &&
        layout.node(target_parent.value()).split().axis == placement.axis) {
      insert_into_matching_split(nodes, target_parent.value(), placement.insertion);
      return;
    }
    wrap_target(nodes, target_parent, placement);
  }

  void insert_into_matching_split(NodeRelocation const nodes, PaneNodeId const parent_id,
                                  PaneInsertion const insertion) {
    PaneSplit& split = layout.nodes.at(parent_id).mutable_split();
    std::size_t const target_index = child_index(split, nodes.target);
    int const target_share = split.children[target_index].percentage.value();
    int const earlier_share = (target_share + 1) / 2;
    int const later_share = target_share / 2;
    int const source_share = insertion == PaneInsertion::BEFORE ? earlier_share : later_share;
    int const retained_target_share =
        insertion == PaneInsertion::BEFORE ? later_share : earlier_share;

    std::vector<PaneSplitChild> inserted_children;
    flatten_source_for_axis(nodes.source, split.axis, source_share, inserted_children);

    split.children[target_index].percentage = required_percentage(retained_target_share);
    std::size_t const insertion_index =
        insertion == PaneInsertion::BEFORE ? target_index : target_index + 1;
    split.children.insert(split.children.begin() + static_cast<std::ptrdiff_t>(insertion_index),
                          inserted_children.begin(), inserted_children.end());
    for (PaneSplitChild const& child : inserted_children) {
      layout.nodes.at(child.node_id).set_parent(parent_id);
    }
  }

  void flatten_source_for_axis(PaneNodeId const source, PaneSplitAxis const axis,
                               int const source_share, std::vector<PaneSplitChild>& children) {
    PaneLayoutNode const& source_node = layout.node(source);
    if (!source_node.is_leaf() && source_node.split().axis == axis) {
      PaneSplit const source_split = source_node.split();
      children = source_split.children;
      assign_percentages(
          children, distribute_pane_percentage_total(percentage_values(children), source_share));
      layout.nodes.erase(source);
      return;
    }
    children = {{.node_id = source, .percentage = required_percentage(source_share)}};
  }

  void wrap_target(NodeRelocation const nodes, std::optional<PaneNodeId> const target_parent,
                   DropPlacement const placement) {
    PaneNodeId const wrapper_id = layout.allocate_node_id();
    std::vector<PaneSplitChild> source_children;
    flatten_source_for_axis(nodes.source, placement.axis, 50, source_children);

    std::vector<PaneSplitChild> children;
    PaneSplitChild const target_child{
        .node_id = nodes.target,
        .percentage = required_percentage(50),
    };
    if (placement.insertion == PaneInsertion::BEFORE) {
      children = source_children;
      children.push_back(target_child);
    } else {
      children.push_back(target_child);
      children.insert(children.end(), source_children.begin(), source_children.end());
    }

    if (target_parent.has_value()) {
      PaneSplit& parent_split = layout.nodes.at(target_parent.value()).mutable_split();
      parent_split.children[child_index(parent_split, nodes.target)].node_id = wrapper_id;
    } else {
      layout.root = wrapper_id;
    }

    layout.nodes.at(nodes.target).set_parent(wrapper_id);
    for (PaneSplitChild const& child : source_children) {
      layout.nodes.at(child.node_id).set_parent(wrapper_id);
    }
    layout.nodes.emplace(wrapper_id, PaneLayoutNode(wrapper_id, target_parent,
                                                    PaneSplit{.axis = placement.axis,
                                                              .children = std::move(children)}));
  }

  void normalize_direct_children(PaneNodeId const parent_id) {
    PaneSplit& split = layout.nodes.at(parent_id).mutable_split();
    std::vector<PaneSplitChild> children;
    std::vector<PaneNodeId> flattened_nodes;
    for (PaneSplitChild const& split_child : split.children) {
      PaneNodeId const child_id = split_child.node_id;
      PaneLayoutNode const& child = layout.node(child_id);
      if (child.is_leaf() || child.split().axis != split.axis) {
        children.push_back(split_child);
        continue;
      }

      PaneSplit const child_split = child.split();
      std::vector<PaneSplitChild> promoted_children = child_split.children;
      assign_percentages(promoted_children,
                         distribute_pane_percentage_total(percentage_values(promoted_children),
                                                          split_child.percentage.value()));
      children.insert(children.end(), promoted_children.begin(), promoted_children.end());
      flattened_nodes.push_back(child_id);
    }
    split.children = std::move(children);
    for (PaneSplitChild const& child : split.children) {
      layout.nodes.at(child.node_id).set_parent(parent_id);
    }
    for (PaneNodeId const flattened : flattened_nodes) {
      layout.nodes.erase(flattened);
    }
  }

  PaneLayout& layout;
};

bool move_pane_selection(PaneLayout& layout, PaneSelection const& selection,
                         PaneNodeId const target, PaneDropDirection const direction) {
  PaneLayout moved = layout;
  bool const changed = PaneLayoutMover(moved).move(selection, target, direction);
  if (changed) {
    layout = std::move(moved);
  }
  return changed;
}

bool swap_pane_nodes(PaneLayout& layout, PaneNodeId const source, PaneNodeId const target) {
  PaneLayout swapped = layout;
  bool const changed = PaneLayoutMover(swapped).exchange_nodes(source, target);
  if (changed) {
    layout = std::move(swapped);
  }
  return changed;
}

}  // namespace moe::parent
