#include "src/parent/pane/pane_resize.h"

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

struct ThreePaneLayout {
  moe::parent::PaneLayout layout;
  moe::parent::PaneNodeId first;
  moe::parent::PaneNodeId second;
  moe::parent::PaneNodeId third;
};

ThreePaneLayout make_three_pane_layout() {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  return {.layout = std::move(layout), .first = first, .second = second, .third = third};
}

std::vector<int> percentages(moe::parent::PaneLayout const& layout) {
  std::vector<int> result;
  for (moe::parent::PaneSplitChild const& child : layout.node(layout.root_id()).split().children) {
    result.push_back(child.percentage.value());
  }
  return result;
}

TEST(PaneResizeTest, GrowingContiguousGroupPreservesGroupProportions) {
  ThreePaneLayout panes = make_three_pane_layout();
  moe::parent::PaneSelection const last_two =
      moe::parent::PaneSelection::range(panes.layout, panes.second, panes.third);

  EXPECT_TRUE(moe::parent::resize_pane_selection(panes.layout, last_two, 10));

  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{40, 30, 30}));
}

TEST(PaneResizeTest, ShrinkingGroupRedistributesSpaceToOtherSiblings) {
  ThreePaneLayout panes = make_three_pane_layout();
  moe::parent::PaneSelection const last_two =
      moe::parent::PaneSelection::range(panes.layout, panes.second, panes.third);

  EXPECT_TRUE(moe::parent::resize_pane_selection(panes.layout, last_two, -20));

  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{70, 15, 15}));
}

TEST(PaneResizeTest, ResizeClampsAtZeroAndOneHundred) {
  ThreePaneLayout panes = make_three_pane_layout();
  moe::parent::PaneSelection const first =
      moe::parent::PaneSelection::single(panes.layout, panes.first);

  EXPECT_TRUE(moe::parent::resize_pane_selection(panes.layout, first, 500));
  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{100, 0, 0}));
  EXPECT_FALSE(moe::parent::resize_pane_selection(panes.layout, first, 5));
  EXPECT_TRUE(moe::parent::resize_pane_selection(panes.layout, first, -500));
  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{0, 50, 50}));
}

TEST(PaneResizeTest, GrowingCollapsedPaneUsesItsRequestedShare) {
  ThreePaneLayout panes = make_three_pane_layout();
  panes.layout.set_split_percentages(panes.layout.root_id(), {0, 1, 1});
  moe::parent::PaneSelection const first =
      moe::parent::PaneSelection::single(panes.layout, panes.first);

  EXPECT_TRUE(moe::parent::resize_pane_selection(panes.layout, first, 10));

  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{10, 45, 45}));
}

TEST(PaneResizeTest, SelectingEverySiblingCannotResizeTheLevel) {
  ThreePaneLayout panes = make_three_pane_layout();
  moe::parent::PaneSelection const all =
      moe::parent::PaneSelection::range(panes.layout, panes.first, panes.third);

  EXPECT_FALSE(moe::parent::resize_pane_selection(panes.layout, all, 10));
  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{50, 25, 25}));
}

TEST(PaneResizeTest, EqualizeAppliesToTheSelectionsSiblingLevel) {
  ThreePaneLayout panes = make_three_pane_layout();
  moe::parent::PaneSelection const second =
      moe::parent::PaneSelection::single(panes.layout, panes.second);

  EXPECT_TRUE(moe::parent::equalize_pane_selection_level(panes.layout, second));
  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{34, 33, 33}));
  EXPECT_FALSE(moe::parent::equalize_pane_selection_level(panes.layout, second));
}

TEST(PaneResizeTest, EqualizeSelectionPreservesUnselectedSiblingPercentage) {
  ThreePaneLayout panes = make_three_pane_layout();
  panes.layout.set_split_percentages(panes.layout.root_id(), {60, 25, 15});
  moe::parent::PaneSelection const last_two =
      moe::parent::PaneSelection::range(panes.layout, panes.second, panes.third);

  EXPECT_TRUE(moe::parent::equalize_pane_selection(panes.layout, last_two));
  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{60, 20, 20}));
  EXPECT_FALSE(moe::parent::equalize_pane_selection(panes.layout, last_two));
}

TEST(PaneResizeTest, EqualizeSingletonSplitAppliesToItsDirectChildren) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const second =
      layout.split_leaf(layout.root_id(), moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  static_cast<void>(layout.split_leaf(second, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                                      moe::parent::PaneInsertion::AFTER));
  layout.set_split_percentages(layout.root_id(), {70, 30});
  moe::parent::PaneSelection const root =
      moe::parent::PaneSelection::single(layout, layout.root_id());

  EXPECT_TRUE(moe::parent::equalize_pane_selection(layout, root));
  EXPECT_EQ(percentages(layout), (std::vector<int>{50, 50}));
  EXPECT_FALSE(moe::parent::equalize_pane_selection(layout, root));
}

TEST(PaneResizeTest, EqualizeSingletonLeafDoesNothing) {
  ThreePaneLayout panes = make_three_pane_layout();
  moe::parent::PaneSelection const leaf =
      moe::parent::PaneSelection::single(panes.layout, panes.second);

  EXPECT_FALSE(moe::parent::equalize_pane_selection(panes.layout, leaf));
  EXPECT_EQ(percentages(panes.layout), (std::vector<int>{50, 25, 25}));
}

TEST(PaneResizeTest, RootSelectionHasNoSiblingLevelToResize) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneSelection const root =
      moe::parent::PaneSelection::single(layout, layout.root_id());

  EXPECT_FALSE(moe::parent::resize_pane_selection(layout, root, 10));
  EXPECT_FALSE(moe::parent::equalize_pane_selection(layout, root));
  EXPECT_FALSE(moe::parent::equalize_pane_selection_level(layout, root));
}

}  // namespace
