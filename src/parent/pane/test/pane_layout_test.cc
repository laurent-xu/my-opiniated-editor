#include "src/parent/pane/pane_layout.h"

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

std::vector<moe::parent::PaneId::Value> pane_values(moe::parent::PaneLayout const& layout) {
  std::vector<moe::parent::PaneId::Value> result;
  for (moe::parent::PaneNodeId const node_id : layout.leaf_nodes()) {
    result.push_back(layout.node(node_id).pane_id().value());
  }
  return result;
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

TEST(PaneLayoutTest, StartsWithOneRootLeaf) {
  moe::parent::PaneLayout const layout = moe::parent::PaneLayout::single(pane_id(7));

  EXPECT_TRUE(layout.node(layout.root_id()).is_leaf());
  EXPECT_EQ(layout.node(layout.root_id()).pane_id().value(), 7);
  EXPECT_FALSE(layout.node(layout.root_id()).parent().has_value());
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{7}));
}

TEST(PaneLayoutTest, SameAxisSplitsFlattenIntoOneNaryLevel) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const second =
      layout.split_leaf(layout.root_id(), moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const root = layout.root_id();
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);

  moe::parent::PaneSplit const& split = layout.node(root).split();
  EXPECT_EQ(split.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  ASSERT_EQ(split.children.size(), 3);
  EXPECT_EQ(split.children[1].node_id, second);
  EXPECT_EQ(split.children[2].node_id, third);
  EXPECT_EQ(percentages(split), (std::vector<int>{50, 25, 25}));
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{1, 2, 3}));
}

TEST(PaneLayoutTest, DifferentAxisSplitCreatesOneNestedLevel) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const second =
      layout.split_leaf(layout.root_id(), moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);

  moe::parent::PaneSplit const& root_split = layout.node(layout.root_id()).split();
  ASSERT_EQ(root_split.children.size(), 2);
  moe::parent::PaneNodeId const nested_id = root_split.children[1].node_id;
  moe::parent::PaneSplit const& nested_split = layout.node(nested_id).split();
  EXPECT_EQ(nested_split.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(nested_split), (std::vector<moe::parent::PaneNodeId>{second, third}));
  EXPECT_EQ(percentages(root_split), (std::vector<int>{50, 50}));
  EXPECT_EQ(percentages(nested_split), (std::vector<int>{50, 50}));
  EXPECT_EQ(layout.node(second).parent(), nested_id);
  EXPECT_EQ(layout.node(third).parent(), nested_id);
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{1, 2, 3}));
}

TEST(PaneLayoutTest, BeforeInsertionAssignsOddRemainderToEarlierPane) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const second =
      layout.split_leaf(layout.root_id(), moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  static_cast<void>(layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                                      moe::parent::PaneInsertion::AFTER));
  moe::parent::PaneNodeId const fourth =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(4),
                        moe::parent::PaneInsertion::BEFORE);

  moe::parent::PaneSplit const& split = layout.node(layout.root_id()).split();
  EXPECT_EQ(split.children[1].node_id, fourth);
  EXPECT_EQ(split.children[2].node_id, second);
  EXPECT_EQ(percentages(split), (std::vector<int>{50, 13, 12, 25}));
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{1, 4, 2, 3}));
}

TEST(PaneLayoutTest, FindsPaneAndRejectsDuplicateOrInvalidSplitTargets) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const second =
      layout.split_leaf(layout.root_id(), moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);

  EXPECT_EQ(required(layout.find_pane(pane_id(2))), second);
  EXPECT_THROW(
      static_cast<void>(layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                          pane_id(1), moe::parent::PaneInsertion::AFTER)),
      std::invalid_argument);
  EXPECT_THROW(static_cast<void>(layout.split_leaf(layout.root_id(),
                                                   moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                                   pane_id(3), moe::parent::PaneInsertion::AFTER)),
               std::invalid_argument);
}

TEST(PaneLayoutTest, SetPercentagesNormalizesWeightsAndAllowsCollapse) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  static_cast<void>(layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                                      moe::parent::PaneInsertion::AFTER));

  layout.set_split_percentages(layout.root_id(), {0, 3});

  EXPECT_EQ(percentages(layout.node(layout.root_id()).split()), (std::vector<int>{0, 100}));
  EXPECT_THROW(layout.set_split_percentages(layout.root_id(), {1, 1, 1}), std::invalid_argument);
  EXPECT_THROW(layout.set_split_percentages(first, {100}), std::logic_error);
}

}  // namespace
