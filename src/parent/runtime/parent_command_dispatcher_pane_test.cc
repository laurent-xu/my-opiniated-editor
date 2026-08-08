#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/input/command/pane_command.h"
#include "src/parent/input/command/toggle_command_mode_command.h"
#include "src/parent/input/command/toggle_worktree_overlay_command.h"
#include "src/parent/runtime/parent_command_dispatcher.h"
#include "src/parent/runtime/parent_command_dispatcher_test_support.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/tray/tray_manager_test_support.h"

namespace {

using moe::parent::test_support::command_dispatcher_test_config;
using moe::parent::test_support::COMMAND_DISPATCHER_TEST_SIZE;

template <typename Value>
Value const& required(std::optional<Value> const& value) {
  if (!value.has_value()) {
    throw std::logic_error("expected a value");
  }
  return value.value();
}

moe::parent::ParentCommandDispatchEffects dispatch_pane(
    moe::parent::ParentCommandDispatcher& dispatcher, moe::parent::PaneCommandAction const action) {
  return dispatcher.dispatch(moe::parent::PaneCommand{.action = action},
                             COMMAND_DISPATCHER_TEST_SIZE);
}

TEST(ParentCommandDispatcherPaneTest, PaneActionsRequireCommandModeAndNoOverlay) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(*trays,
                                                  command_dispatcher_test_config("pane-guard"));

  EXPECT_FALSE(
      dispatch_pane(dispatcher, moe::parent::PaneCommandAction::SPLIT_LEFT_TO_RIGHT).redraw);
  EXPECT_EQ(trays->active_pane_layout().leaf_nodes().size(), 1U);

  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));
  EXPECT_TRUE(
      dispatch_pane(dispatcher, moe::parent::PaneCommandAction::SPLIT_LEFT_TO_RIGHT).redraw);
  EXPECT_EQ(trays->active_pane_layout().leaf_nodes().size(), 2U);

  static_cast<void>(dispatcher.dispatch(moe::parent::ToggleWorktreeOverlayCommand{},
                                        COMMAND_DISPATCHER_TEST_SIZE));
  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));
  EXPECT_FALSE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::CLOSE).redraw);
  EXPECT_EQ(trays->active_pane_layout().leaf_nodes().size(), 2U);
}

TEST(ParentCommandDispatcherPaneTest, DirectionKeysFollowFocusSelectionAndMoveContext) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(*trays,
                                                  command_dispatcher_test_config("pane-context"));
  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));

  moe::parent::PaneId const pane_a = trays->active_focused_pane_id();
  static_cast<void>(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::SPLIT_LEFT_TO_RIGHT));
  moe::parent::PaneId const pane_b = trays->active_focused_pane_id();
  static_cast<void>(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::SPLIT_LEFT_TO_RIGHT));
  moe::parent::PaneId const pane_c = trays->active_focused_pane_id();
  ASSERT_TRUE(trays->focus_active_pane(pane_b));

  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::LEFT).redraw);
  EXPECT_EQ(trays->active_focused_pane_id(), pane_a);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::RIGHT).redraw);
  EXPECT_EQ(trays->active_focused_pane_id(), pane_b);

  ASSERT_TRUE(
      dispatch_pane(dispatcher, moe::parent::PaneCommandAction::TOGGLE_SELECTION_OR_SWAP).redraw);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::RIGHT).redraw);
  ASSERT_EQ(required(trays->active_pane_selection()).nodes().size(), 2U);
  EXPECT_EQ(trays->active_focused_pane_id(), pane_b);

  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::TOGGLE_MOVE).redraw);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::LEFT).redraw);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::CONFIRM_MOVE).redraw);
  EXPECT_EQ(required(trays->active_pane_move_session()).stage(), moe::parent::PaneMoveStage::DROP);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::UP).redraw);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::CONFIRM_MOVE).redraw);

  EXPECT_FALSE(trays->active_pane_move_session().has_value());
  EXPECT_EQ(trays->active_pane_layout().leaf_nodes().size(), 3U);
  EXPECT_EQ(trays->active_focused_pane_id(), pane_b);
  EXPECT_NE(trays->active_focused_pane_id(), pane_c);
}

TEST(ParentCommandDispatcherPaneTest, DirectActionsChangeTheActiveLayout) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(*trays,
                                                  command_dispatcher_test_config("pane-actions"));
  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));

  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::SPLIT_ABOVE_BELOW).redraw);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::GROW).redraw);
  EXPECT_EQ(trays->active_pane_layout()
                .node(trays->active_pane_layout().root_id())
                .split()
                .children[1]
                .percentage.value(),
            55);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::EQUALIZE).redraw);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::TOGGLE_MAXIMIZE).redraw);
  EXPECT_TRUE(trays->active_focused_pane_is_maximized());
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::ROTATE).redraw);
  EXPECT_FALSE(trays->active_focused_pane_is_maximized());
  EXPECT_EQ(trays->active_pane_layout().node(trays->active_pane_layout().root_id()).split().axis,
            moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  ASSERT_TRUE(dispatch_pane(dispatcher, moe::parent::PaneCommandAction::CLOSE).redraw);
  EXPECT_EQ(trays->active_pane_layout().leaf_nodes().size(), 1U);
}

TEST(ParentCommandDispatcherPaneTest, ClosingOnlyPaneReportsDestroyedTray) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(
      *trays, command_dispatcher_test_config("pane-close-last"));
  moe::parent::TraySnapshot const original = trays->active_snapshot();
  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));

  moe::parent::ParentCommandDispatchEffects const effects =
      dispatch_pane(dispatcher, moe::parent::PaneCommandAction::CLOSE);

  EXPECT_TRUE(effects.publish_status);
  EXPECT_TRUE(effects.redraw);
  EXPECT_TRUE(effects.trays_destroyed);
  EXPECT_EQ(trays->active_id(), original.id);
  EXPECT_NE(trays->active_snapshot().child_pid.value(), original.child_pid.value());
}

}  // namespace
