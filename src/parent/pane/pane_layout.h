#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <variant>
#include <vector>

#include "src/parent/pane/pane_id.h"
#include "src/parent/pane/pane_node_id.h"
#include "src/parent/pane/pane_percentage.h"
#include "src/parent/pane/pane_split_axis.h"

namespace moe::parent {

enum class PaneInsertion : std::uint8_t {
  BEFORE,
  AFTER,
};

struct PaneSplitChild {
  PaneNodeId node_id;
  PanePercentage percentage;
};

struct PaneSplit {
  PaneSplitAxis axis;
  std::vector<PaneSplitChild> children;
};

class PaneLayoutNode {
 public:
  [[nodiscard]] PaneNodeId id() const;
  [[nodiscard]] std::optional<PaneNodeId> parent() const;
  [[nodiscard]] bool is_leaf() const;
  [[nodiscard]] PaneId pane_id() const;
  [[nodiscard]] PaneSplit const& split() const;

 private:
  friend class PaneLayout;

  PaneLayoutNode(PaneNodeId id, std::optional<PaneNodeId> parent, PaneId pane_id);
  PaneLayoutNode(PaneNodeId id, std::optional<PaneNodeId> parent, PaneSplit split);

  [[nodiscard]] PaneSplit& mutable_split();
  void set_parent(std::optional<PaneNodeId> parent);

  PaneNodeId node_id;
  std::optional<PaneNodeId> parent_id;
  std::variant<PaneId, PaneSplit> contents;
};

class PaneLayout {
 public:
  [[nodiscard]] static PaneLayout single(PaneId pane_id);

  [[nodiscard]] PaneNodeId root_id() const;
  [[nodiscard]] PaneLayoutNode const& node(PaneNodeId node_id) const;
  [[nodiscard]] std::optional<PaneNodeId> find_pane(PaneId pane_id) const;
  [[nodiscard]] std::vector<PaneNodeId> leaf_nodes() const;

  [[nodiscard]] PaneNodeId split_leaf(PaneNodeId target, PaneSplitAxis axis, PaneId new_pane,
                                      PaneInsertion insertion);
  void set_split_percentages(PaneNodeId split_node, std::vector<int> const& weights);

 private:
  PaneLayout(PaneNodeId root_node, std::map<PaneNodeId, PaneLayoutNode> layout_nodes,
             PaneNodeId::Value next_value);

  [[nodiscard]] PaneNodeId allocate_node_id();
  void append_leaf_nodes(PaneNodeId node_id, std::vector<PaneNodeId>& output) const;

  PaneNodeId root;
  std::map<PaneNodeId, PaneLayoutNode> nodes;
  PaneNodeId::Value next_node_value;
};

}  // namespace moe::parent
