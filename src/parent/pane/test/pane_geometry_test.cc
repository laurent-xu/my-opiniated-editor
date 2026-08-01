#include "src/parent/pane/pane_geometry.h"

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

void expect_region(moe::parent::PaneRegion const& region, int const row, int const column,
                   int const rows, int const cols) {
  EXPECT_EQ(region.origin.row, row);
  EXPECT_EQ(region.origin.column, column);
  EXPECT_EQ(region.size.rows, rows);
  EXPECT_EQ(region.size.cols, cols);
}

TEST(PaneGeometryTest, SinglePaneFillsOuterRegion) {
  moe::parent::PaneLayout const layout = moe::parent::PaneLayout::single(pane_id(1));

  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 2, .column = 3}, .size = {.rows = 7, .cols = 11}});

  expect_region(geometry.region(layout.root_id()), 2, 3, 7, 11);
  EXPECT_TRUE(geometry.separators().empty());
}

TEST(PaneGeometryTest, NarySplitRoundsContentAndReservesSeparatorsExactly) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);

  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 2, .column = 3}, .size = {.rows = 7, .cols = 15}});

  expect_region(geometry.region(first), 2, 3, 7, 7);
  expect_region(geometry.region(second), 2, 11, 7, 3);
  expect_region(geometry.region(third), 2, 15, 7, 3);
  ASSERT_EQ(geometry.separators().size(), 2);
  expect_region(geometry.separators()[0].region, 2, 10, 7, 1);
  expect_region(geometry.separators()[1].region, 2, 14, 7, 1);
}

TEST(PaneGeometryTest, NestedSplitUsesItsParentChildRegion) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const second =
      layout.split_leaf(layout.root_id(), moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const nested = required(layout.node(second).parent());

  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = 7, .cols = 11}});

  expect_region(geometry.region(layout.root_id()), 0, 0, 7, 11);
  expect_region(geometry.region(nested), 0, 6, 7, 5);
  expect_region(geometry.region(second), 0, 6, 3, 5);
  expect_region(geometry.region(third), 4, 6, 3, 5);
  ASSERT_EQ(geometry.separators().size(), 2);
  expect_region(geometry.separators()[0].region, 0, 5, 7, 1);
  expect_region(geometry.separators()[1].region, 3, 6, 1, 5);
}

TEST(PaneGeometryTest, ZeroPercentagePaneCollapsesButKeepsItsNode) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  layout.set_split_percentages(layout.root_id(), {0, 1, 0});

  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = 4, .cols = 8}});

  expect_region(geometry.region(first), 0, 0, 4, 0);
  expect_region(geometry.region(second), 0, 1, 4, 6);
  expect_region(geometry.region(third), 0, 8, 4, 0);
  EXPECT_EQ(layout.leaf_nodes().size(), 3);
}

TEST(PaneGeometryTest, TooNarrowSplitUsesAllCellsAsContent) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);

  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = 4, .cols = 1}});

  expect_region(geometry.region(first), 0, 0, 4, 1);
  expect_region(geometry.region(second), 0, 1, 4, 0);
  expect_region(geometry.region(third), 0, 1, 4, 0);
  EXPECT_TRUE(geometry.separators().empty());
}

TEST(PaneGeometryTest, RejectsNegativeOuterSize) {
  moe::parent::PaneLayout const layout = moe::parent::PaneLayout::single(pane_id(1));
  EXPECT_THROW(static_cast<void>(moe::parent::calculate_pane_geometry(
                   layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = -1, .cols = 10}})),
               std::invalid_argument);
}

}  // namespace
