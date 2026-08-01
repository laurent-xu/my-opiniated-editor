#include "src/parent/pane/pane_navigation.h"

#include <optional>
#include <stdexcept>

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

TEST(PaneNavigationTest, MovesAcrossNarySiblingsWithoutWrapping) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = 20, .cols = 90}});

  EXPECT_EQ(moe::parent::find_directional_pane(layout, geometry, second,
                                               moe::parent::PaneFocusDirection::LEFT),
            first);
  EXPECT_EQ(moe::parent::find_directional_pane(layout, geometry, second,
                                               moe::parent::PaneFocusDirection::RIGHT),
            third);
  EXPECT_FALSE(moe::parent::find_directional_pane(layout, geometry, first,
                                                  moe::parent::PaneFocusDirection::LEFT)
                   .has_value());
  EXPECT_FALSE(moe::parent::find_directional_pane(layout, geometry, third,
                                                  moe::parent::PaneFocusDirection::RIGHT)
                   .has_value());
}

TEST(PaneNavigationTest, MovesGeometricallyAcrossNestedSplits) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const left = layout.root_id();
  moe::parent::PaneNodeId const top_right =
      layout.split_leaf(left, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const bottom_right =
      layout.split_leaf(top_right, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = 21, .cols = 81}});

  EXPECT_EQ(moe::parent::find_directional_pane(layout, geometry, left,
                                               moe::parent::PaneFocusDirection::RIGHT),
            top_right);
  EXPECT_EQ(moe::parent::find_directional_pane(layout, geometry, top_right,
                                               moe::parent::PaneFocusDirection::LEFT),
            left);
  EXPECT_EQ(moe::parent::find_directional_pane(layout, geometry, top_right,
                                               moe::parent::PaneFocusDirection::DOWN),
            bottom_right);
  EXPECT_EQ(moe::parent::find_directional_pane(layout, geometry, bottom_right,
                                               moe::parent::PaneFocusDirection::UP),
            top_right);
  EXPECT_EQ(moe::parent::find_directional_pane(layout, geometry, bottom_right,
                                               moe::parent::PaneFocusDirection::LEFT),
            left);
}

TEST(PaneNavigationTest, SkipsCollapsedCandidates) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  layout.set_split_percentages(layout.root_id(), {50, 0, 50});
  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = 20, .cols = 90}});

  EXPECT_EQ(moe::parent::find_directional_pane(layout, geometry, first,
                                               moe::parent::PaneFocusDirection::RIGHT),
            third);
}

TEST(PaneNavigationTest, RejectsSplitNodeAsSource) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  static_cast<void>(layout.split_leaf(layout.root_id(), moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                      pane_id(2), moe::parent::PaneInsertion::AFTER));
  moe::parent::PaneGeometry const geometry = moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = 20, .cols = 90}});

  EXPECT_THROW(static_cast<void>(moe::parent::find_directional_pane(
                   layout, geometry, layout.root_id(), moe::parent::PaneFocusDirection::RIGHT)),
               std::invalid_argument);
}

}  // namespace
