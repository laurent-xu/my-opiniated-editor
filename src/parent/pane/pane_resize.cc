#include "src/parent/pane/pane_resize.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace moe::parent {

namespace {

struct SelectionPartition {
  std::vector<std::size_t> selected_indices;
  std::vector<std::size_t> unselected_indices;
};

SelectionPartition partition_selection(PaneSplit const& split, PaneSelection const& selection) {
  SelectionPartition partition;
  for (std::size_t index = 0; index < split.children.size(); ++index) {
    std::vector<std::size_t>& destination = selection.contains(split.children[index].node_id)
                                                ? partition.selected_indices
                                                : partition.unselected_indices;
    destination.push_back(index);
  }
  if (partition.selected_indices.size() != selection.nodes().size()) {
    throw std::invalid_argument("pane selection is stale or belongs to another split");
  }
  return partition;
}

std::vector<int> weights_at(PaneSplit const& split, std::vector<std::size_t> const& indices) {
  std::vector<int> result;
  result.reserve(indices.size());
  for (std::size_t const index : indices) {
    result.push_back(split.children[index].percentage.value());
  }
  return result;
}

std::vector<int> percentage_values(PaneSplit const& split) {
  std::vector<int> result;
  result.reserve(split.children.size());
  for (PaneSplitChild const& child : split.children) {
    result.push_back(child.percentage.value());
  }
  return result;
}

void assign_at(std::vector<int>& destination, std::vector<std::size_t> const& indices,
               std::vector<int> const& values) {
  if (indices.size() != values.size()) {
    throw std::logic_error("pane percentage assignment size mismatch");
  }
  for (std::size_t index = 0; index < indices.size(); ++index) {
    destination[indices[index]] = values[index];
  }
}

bool equalize_split(PaneLayout& layout, PaneNodeId const node_id) {
  PaneSplit const& split = layout.node(node_id).split();
  std::vector<PanePercentage> const equal =
      equal_pane_percentages(static_cast<int>(split.children.size()));
  std::vector<int> percentages;
  percentages.reserve(equal.size());
  for (PanePercentage const percentage : equal) {
    percentages.push_back(percentage.value());
  }
  if (percentages == percentage_values(split)) {
    return false;
  }
  layout.set_split_percentages(node_id, percentages);
  return true;
}

}  // namespace

bool resize_pane_selection(PaneLayout& layout, PaneSelection const& selection,
                           int const delta_percentage) {
  if (!selection.parent().has_value() || delta_percentage == 0) {
    return false;
  }

  PaneNodeId const parent = selection.parent().value();
  PaneSplit const& split = layout.node(parent).split();
  SelectionPartition const partition = partition_selection(split, selection);
  if (partition.unselected_indices.empty()) {
    return false;
  }

  std::vector<int> const selected_weights = weights_at(split, partition.selected_indices);
  std::vector<int> const unselected_weights = weights_at(split, partition.unselected_indices);
  int const selected_total = std::accumulate(selected_weights.begin(), selected_weights.end(), 0);
  int const resized_total = std::clamp(selected_total + delta_percentage, PanePercentage::MIN_VALUE,
                                       PanePercentage::MAX_VALUE);
  if (resized_total == selected_total) {
    return false;
  }

  std::vector<int> percentages(split.children.size(), 0);
  assign_at(percentages, partition.selected_indices,
            distribute_pane_percentage_total(selected_weights, resized_total));
  assign_at(percentages, partition.unselected_indices,
            distribute_pane_percentage_total(unselected_weights,
                                             PanePercentage::MAX_VALUE - resized_total));
  layout.set_split_percentages(parent, percentages);
  return true;
}

bool equalize_pane_selection(PaneLayout& layout, PaneSelection const& selection) {
  if (selection.nodes().size() == 1U) {
    PaneNodeId const selected = selection.nodes().front();
    if (!layout.node(selected).is_leaf()) {
      return equalize_split(layout, selected);
    }
  }
  if (!selection.parent().has_value()) {
    return false;
  }

  PaneNodeId const parent = selection.parent().value();
  PaneSplit const& split = layout.node(parent).split();
  SelectionPartition const partition = partition_selection(split, selection);
  std::vector<int> const selected_weights = weights_at(split, partition.selected_indices);
  int const selected_total = std::accumulate(selected_weights.begin(), selected_weights.end(), 0);
  std::vector<int> const current = percentage_values(split);
  std::vector<int> percentages = current;
  assign_at(percentages, partition.selected_indices,
            distribute_pane_percentage_total(std::vector<int>(partition.selected_indices.size(), 1),
                                             selected_total));
  if (percentages == current) {
    return false;
  }
  layout.set_split_percentages(parent, percentages);
  return true;
}

bool equalize_pane_selection_level(PaneLayout& layout, PaneSelection const& selection) {
  if (!selection.parent().has_value()) {
    return false;
  }

  PaneNodeId const parent = selection.parent().value();
  return equalize_split(layout, parent);
}

}  // namespace moe::parent
