#include "src/parent/pane/pane_move.h"

#include <optional>
#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"

namespace {

template <typename Value>
Value const& required(std::optional<Value> const& value) {
  if (!value.has_value()) {
    throw std::logic_error("expected a value");
  }
  return value.value();
}

moe::parent::PaneId pane_id(moe::parent::PaneId::Value const value) {
  return required(moe::parent::PaneId::from_value(value));
}

std::vector<int> percentages(moe::parent::PaneSplit const& split) {
  std::vector<int> result;
  result.reserve(split.children.size());
  for (moe::parent::PaneSplitChild const& child : split.children) {
    result.push_back(child.percentage.value());
  }
  return result;
}

std::vector<moe::parent::PaneNodeId> child_node_ids(moe::parent::PaneSplit const& split) {
  std::vector<moe::parent::PaneNodeId> result;
  result.reserve(split.children.size());
  for (moe::parent::PaneSplitChild const& child : split.children) {
    result.push_back(child.node_id);
  }
  return result;
}

std::vector<moe::parent::PaneId::Value> pane_values(moe::parent::PaneLayout const& layout) {
  std::vector<moe::parent::PaneId::Value> result;
  for (moe::parent::PaneNodeId const leaf : layout.leaf_nodes()) {
    result.push_back(layout.node(leaf).pane_id().value());
  }
  return result;
}

void expect_normalized_subtree(moe::parent::PaneLayout const& layout,
                               moe::parent::PaneNodeId const node_id) {
  moe::parent::PaneLayoutNode const& node = layout.node(node_id);
  if (node.is_leaf()) {
    return;
  }
  moe::parent::PaneSplit const& split = node.split();
  ASSERT_GE(split.children.size(), 2U);
  int total = 0;
  for (moe::parent::PaneSplitChild const& split_child : split.children) {
    moe::parent::PaneLayoutNode const& child = layout.node(split_child.node_id);
    EXPECT_EQ(child.parent(), node_id);
    if (!child.is_leaf()) {
      EXPECT_NE(child.split().axis, split.axis);
    }
    total += split_child.percentage.value();
    expect_normalized_subtree(layout, split_child.node_id);
  }
  EXPECT_EQ(total, 100);
}

void expect_normalized(moe::parent::PaneLayout const& layout) {
  EXPECT_FALSE(layout.node(layout.root_id()).parent().has_value());
  expect_normalized_subtree(layout, layout.root_id());
}

TEST(PaneMoveTest, SameAxisMoveReordersNarySiblingsWithTheirPercentages) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneSelection const selection = moe::parent::PaneSelection::single(layout, second);

  ASSERT_TRUE(moe::parent::move_pane_selection(layout, selection, first,
                                               moe::parent::PaneDropDirection::LEFT));

  moe::parent::PaneSplit const& split = layout.node(layout.root_id()).split();
  EXPECT_EQ(child_node_ids(split), (std::vector<moe::parent::PaneNodeId>{second, first, third}));
  EXPECT_EQ(percentages(split), (std::vector<int>{25, 50, 25}));
  expect_normalized(layout);
  EXPECT_FALSE(moe::parent::move_pane_selection(layout,
                                                moe::parent::PaneSelection::single(layout, second),
                                                first, moe::parent::PaneDropDirection::LEFT));
}

TEST(PaneMoveTest, MovesVerticalGroupBesideHorizontalTargetAsCompleteNode) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const group_bc = required(layout.node(pane_b).parent());
  moe::parent::PaneSelection const selection =
      moe::parent::PaneSelection::range(layout, pane_b, pane_c);

  ASSERT_TRUE(moe::parent::move_pane_selection(layout, selection, pane_a,
                                               moe::parent::PaneDropDirection::LEFT));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  EXPECT_EQ(root.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(child_node_ids(root), (std::vector<moe::parent::PaneNodeId>{group_bc, pane_a}));
  EXPECT_EQ(percentages(root), (std::vector<int>{50, 50}));
  moe::parent::PaneSplit const& moved_group = layout.node(group_bc).split();
  EXPECT_EQ(moved_group.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(moved_group), (std::vector<moe::parent::PaneNodeId>{pane_b, pane_c}));
  expect_normalized(layout);
}

TEST(PaneMoveTest, PromotesUnarySourceAndFlattensMatchingDestinationAxis) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);

  ASSERT_TRUE(moe::parent::move_pane_selection(layout,
                                               moe::parent::PaneSelection::single(layout, pane_b),
                                               pane_a, moe::parent::PaneDropDirection::RIGHT));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  EXPECT_EQ(root.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(child_node_ids(root), (std::vector<moe::parent::PaneNodeId>{pane_a, pane_b, pane_c}));
  EXPECT_EQ(percentages(root), (std::vector<int>{25, 25, 50}));
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{1, 2, 3}));
  expect_normalized(layout);
}

TEST(PaneMoveTest, DifferentAxisDropWrapsTargetAndBecomesNormalizedRoot) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);

  ASSERT_TRUE(moe::parent::move_pane_selection(layout,
                                               moe::parent::PaneSelection::single(layout, second),
                                               first, moe::parent::PaneDropDirection::DOWN));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  EXPECT_EQ(root.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(root), (std::vector<moe::parent::PaneNodeId>{first, second}));
  EXPECT_EQ(percentages(root), (std::vector<int>{50, 50}));
  expect_normalized(layout);
}

TEST(PaneMoveTest, PartialRangeKeepsItsOldAxisInsideDifferentAxisDrop) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_d =
      layout.split_leaf(pane_c, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(4),
                        moe::parent::PaneInsertion::AFTER);

  ASSERT_TRUE(moe::parent::move_pane_selection(
      layout, moe::parent::PaneSelection::range(layout, pane_b, pane_c), pane_a,
      moe::parent::PaneDropDirection::DOWN));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  ASSERT_EQ(root.children.size(), 2U);
  EXPECT_EQ(root.children[1].node_id, pane_d);
  moe::parent::PaneSplit const& vertical = layout.node(root.children[0].node_id).split();
  EXPECT_EQ(vertical.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  ASSERT_EQ(vertical.children.size(), 2U);
  EXPECT_EQ(vertical.children[0].node_id, pane_a);
  moe::parent::PaneSplit const& moved_range = layout.node(vertical.children[1].node_id).split();
  EXPECT_EQ(moved_range.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(child_node_ids(moved_range), (std::vector<moe::parent::PaneNodeId>{pane_b, pane_c}));
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{1, 2, 3, 4}));
  expect_normalized(layout);
}

TEST(PaneMoveTest, MatchingMovedGroupFlattensIntoOneDestinationLevel) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_d =
      layout.split_leaf(pane_c, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(4),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneSelection const top_group =
      moe::parent::PaneSelection::range(layout, pane_a, pane_b);

  ASSERT_TRUE(moe::parent::move_pane_selection(layout, top_group, pane_c,
                                               moe::parent::PaneDropDirection::LEFT));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  EXPECT_EQ(root.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(child_node_ids(root),
            (std::vector<moe::parent::PaneNodeId>{pane_a, pane_b, pane_c, pane_d}));
  EXPECT_EQ(percentages(root), (std::vector<int>{13, 12, 25, 50}));
  expect_normalized(layout);
}

TEST(PaneMoveTest, RejectsAncestorTargetWithoutMutatingLayout) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const group_bc = required(layout.node(pane_b).parent());
  std::vector<moe::parent::PaneId::Value> const before = pane_values(layout);

  EXPECT_THROW(static_cast<void>(moe::parent::move_pane_selection(
                   layout, moe::parent::PaneSelection::single(layout, pane_b), group_bc,
                   moe::parent::PaneDropDirection::LEFT)),
               std::invalid_argument);

  EXPECT_EQ(pane_values(layout), before);
  EXPECT_EQ(child_node_ids(layout.node(group_bc).split()),
            (std::vector<moe::parent::PaneNodeId>{pane_b, pane_c}));
  expect_normalized(layout);
}

TEST(PaneMoveTest, SwapsCompleteSiblingSlotsWithoutMovingPercentages) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);

  ASSERT_TRUE(moe::parent::swap_pane_nodes(layout, first, third));

  moe::parent::PaneSplit const& split = layout.node(layout.root_id()).split();
  EXPECT_EQ(child_node_ids(split), (std::vector<moe::parent::PaneNodeId>{third, second, first}));
  EXPECT_EQ(percentages(split), (std::vector<int>{50, 25, 25}));
  expect_normalized(layout);
}

TEST(PaneMoveTest, SwapsCompleteNodesAcrossDifferentLevels) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const group_bc = required(layout.node(pane_b).parent());

  ASSERT_TRUE(moe::parent::swap_pane_nodes(layout, pane_a, pane_b));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  EXPECT_EQ(child_node_ids(root), (std::vector<moe::parent::PaneNodeId>{pane_b, group_bc}));
  EXPECT_EQ(percentages(root), (std::vector<int>{50, 50}));
  EXPECT_EQ(child_node_ids(layout.node(group_bc).split()),
            (std::vector<moe::parent::PaneNodeId>{pane_a, pane_c}));
  EXPECT_EQ(percentages(layout.node(group_bc).split()), (std::vector<int>{50, 50}));
  expect_normalized(layout);
}

TEST(PaneMoveTest, SwapFlattensMatchingAxisGroupAtDestination) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_d =
      layout.split_leaf(pane_c, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(4),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const group_ab = required(layout.node(pane_a).parent());
  moe::parent::PaneNodeId const group_cd = required(layout.node(pane_c).parent());

  ASSERT_TRUE(moe::parent::swap_pane_nodes(layout, group_ab, pane_c));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  EXPECT_EQ(child_node_ids(root), (std::vector<moe::parent::PaneNodeId>{pane_c, group_cd}));
  moe::parent::PaneSplit const& destination = layout.node(group_cd).split();
  EXPECT_EQ(child_node_ids(destination),
            (std::vector<moe::parent::PaneNodeId>{pane_a, pane_b, pane_d}));
  EXPECT_EQ(percentages(destination), (std::vector<int>{25, 25, 50}));
  EXPECT_THROW(static_cast<void>(layout.node(group_ab)), std::out_of_range);
  expect_normalized(layout);
}

TEST(PaneMoveTest, RejectsAncestorSwapWithoutMutation) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);

  EXPECT_THROW(static_cast<void>(moe::parent::swap_pane_nodes(layout, layout.root_id(), second)),
               std::invalid_argument);

  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{1, 2}));
  expect_normalized(layout);
}

}  // namespace
