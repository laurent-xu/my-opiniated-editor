#include "src/parent/pane/pane_move_session.h"

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
  for (moe::parent::PaneNodeId const leaf : layout.leaf_nodes()) {
    result.push_back(layout.node(leaf).pane_id().value());
  }
  return result;
}

moe::parent::PaneGeometry geometry_for(moe::parent::PaneLayout const& layout) {
  return moe::parent::calculate_pane_geometry(
      layout, {.origin = {.row = 0, .column = 0}, .size = {.rows = 24, .cols = 80}});
}

TEST(PaneMoveSessionTest, NavigatesAndPromotesOnlyEligibleCompleteTargets) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const group_bc = required(layout.node(pane_b).parent());
  moe::parent::PaneMoveSession session =
      moe::parent::PaneMoveSession::begin(moe::parent::PaneSelection::single(layout, pane_a));

  ASSERT_TRUE(
      session.step_target(layout, geometry_for(layout), moe::parent::PaneFocusDirection::RIGHT));
  EXPECT_EQ(session.target(), pane_b);
  ASSERT_TRUE(session.promote_target(layout));
  EXPECT_EQ(session.target(), group_bc);
  EXPECT_FALSE(session.promote_target(layout));
  ASSERT_TRUE(session.descend_target(layout));
  EXPECT_EQ(session.target(), pane_b);
  static_cast<void>(pane_c);
}

TEST(PaneMoveSessionTest, SelectedGroupSkipsItsOwnLeavesWhenChoosingTarget) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneMoveSession session = moe::parent::PaneMoveSession::begin(
      moe::parent::PaneSelection::range(layout, pane_b, pane_c));

  ASSERT_TRUE(
      session.step_target(layout, geometry_for(layout), moe::parent::PaneFocusDirection::LEFT));
  EXPECT_EQ(session.target(), pane_a);
  EXPECT_FALSE(session.promote_target(layout));
}

TEST(PaneMoveSessionTest, LocksTargetThenPreviewsAndConfirmsDrop) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneMoveSession session =
      moe::parent::PaneMoveSession::begin(moe::parent::PaneSelection::single(layout, second));

  ASSERT_TRUE(
      session.step_target(layout, geometry_for(layout), moe::parent::PaneFocusDirection::RIGHT));
  EXPECT_EQ(session.target(), third);
  ASSERT_TRUE(session.lock_target());
  EXPECT_EQ(session.stage(), moe::parent::PaneMoveStage::DROP);
  EXPECT_FALSE(session.preview(layout).has_value());
  ASSERT_TRUE(session.set_drop_direction(moe::parent::PaneDropDirection::RIGHT));

  std::optional<moe::parent::PaneLayout> const preview = session.preview(layout);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(pane_values(required(preview)), (std::vector<moe::parent::PaneId::Value>{1, 3, 2}));
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{1, 2, 3}));

  ASSERT_TRUE(session.confirm(layout));
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{1, 3, 2}));
}

TEST(PaneMoveSessionTest, SwapPreviewsAndConfirmsWithoutDropStage) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneMoveSession session =
      moe::parent::PaneMoveSession::begin(moe::parent::PaneSelection::single(layout, first));

  ASSERT_TRUE(
      session.step_target(layout, geometry_for(layout), moe::parent::PaneFocusDirection::RIGHT));
  EXPECT_EQ(session.target(), second);
  ASSERT_TRUE(session.toggle_swap());
  EXPECT_EQ(session.operation(), moe::parent::PaneMoveOperation::SWAP);
  EXPECT_FALSE(session.lock_target());
  ASSERT_TRUE(session.preview(layout).has_value());

  ASSERT_TRUE(session.confirm(layout));
  EXPECT_EQ(pane_values(layout), (std::vector<moe::parent::PaneId::Value>{2, 1}));
}

TEST(PaneMoveSessionTest, MultiNodeSourceCannotRequestSwap) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneMoveSession session =
      moe::parent::PaneMoveSession::begin(moe::parent::PaneSelection::range(layout, first, second));

  EXPECT_FALSE(session.toggle_swap());
  ASSERT_TRUE(
      session.step_target(layout, geometry_for(layout), moe::parent::PaneFocusDirection::RIGHT));
  EXPECT_EQ(session.target(), third);
  EXPECT_EQ(session.operation(), moe::parent::PaneMoveOperation::MOVE);
}

}  // namespace
