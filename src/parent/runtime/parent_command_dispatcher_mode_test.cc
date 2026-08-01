#include <memory>

#include "gtest/gtest.h"
#include "src/parent/input/begin_tray_action_command.h"
#include "src/parent/input/toggle_command_mode_command.h"
#include "src/parent/input/toggle_worktree_overlay_command.h"
#include "src/parent/input/tray_action_intent.h"
#include "src/parent/runtime/parent_command_dispatcher.h"
#include "src/parent/runtime/parent_command_dispatcher_test_support.h"
#include "src/parent/tray/tray_action_kind.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/tray/tray_manager_test_support.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"

namespace {

using moe::parent::test_support::command_dispatcher_test_config;
using moe::parent::test_support::COMMAND_DISPATCHER_TEST_SIZE;

TEST(ParentCommandDispatcherModeTest, ToggleCommandOwnsCommandModeTransition) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(
      *trays, command_dispatcher_test_config("toggle-command-mode"));

  EXPECT_FALSE(dispatcher.command_mode());

  moe::parent::ParentCommandDispatchEffects const enabled =
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_TRUE(dispatcher.command_mode());
  EXPECT_TRUE(enabled.publish_status);
  EXPECT_FALSE(enabled.redraw);
  EXPECT_FALSE(enabled.trays_destroyed);

  moe::parent::ParentCommandDispatchEffects const disabled =
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_FALSE(dispatcher.command_mode());
  EXPECT_TRUE(disabled.publish_status);
  EXPECT_FALSE(disabled.redraw);
  EXPECT_FALSE(disabled.trays_destroyed);
}

TEST(ParentCommandDispatcherModeTest, TrayActionCommandsRemainGuardedByCommandMode) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(
      *trays, command_dispatcher_test_config("tray-action-guard"));
  moe::parent::BeginTrayActionCommand const clear{
      .action = moe::parent::TrayActionIntent::CLEAR,
  };

  moe::parent::ParentCommandDispatchEffects const ignored =
      dispatcher.dispatch(clear, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_FALSE(ignored.publish_status);
  EXPECT_FALSE(ignored.redraw);
  EXPECT_FALSE(ignored.trays_destroyed);

  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));
  moe::parent::ParentCommandDispatchEffects const handled =
      dispatcher.dispatch(clear, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_TRUE(handled.publish_status);
  EXPECT_TRUE(handled.redraw);
  EXPECT_FALSE(handled.trays_destroyed);
}

TEST(ParentCommandDispatcherModeTest, OpeningOverlayLeavesCommandModeAndClosingPreservesIt) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(*trays,
                                                  command_dispatcher_test_config("toggle-overlay"));
  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));

  moe::parent::ParentCommandDispatchEffects const opened = dispatcher.dispatch(
      moe::parent::ToggleWorktreeOverlayCommand{}, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_FALSE(dispatcher.command_mode());
  EXPECT_NE(trays->active_worktree_management_overlay(), nullptr);
  EXPECT_TRUE(opened.publish_status);
  EXPECT_TRUE(opened.redraw);

  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));
  moe::parent::ParentCommandDispatchEffects const closed = dispatcher.dispatch(
      moe::parent::ToggleWorktreeOverlayCommand{}, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_TRUE(dispatcher.command_mode());
  EXPECT_EQ(trays->active_worktree_management_overlay(), nullptr);
  EXPECT_TRUE(closed.publish_status);
  EXPECT_TRUE(closed.redraw);
}

TEST(ParentCommandDispatcherModeTest, TogglingCommandModeCancelsTrayActionConfirmation) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(
      *trays, command_dispatcher_test_config("cancel-confirmation"));
  static_cast<void>(dispatcher.dispatch(moe::parent::ToggleWorktreeOverlayCommand{},
                                        COMMAND_DISPATCHER_TEST_SIZE));
  moe::parent::WorktreeManagementOverlay* const overlay =
      trays->active_worktree_management_overlay();
  ASSERT_NE(overlay, nullptr);
  ASSERT_TRUE(overlay->begin_tray_action_confirmation(moe::parent::TrayActionKind::CLEAR));

  moe::parent::ParentCommandDispatchEffects const effects =
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE);

  EXPECT_TRUE(dispatcher.command_mode());
  EXPECT_FALSE(overlay->has_tray_action_confirmation());
  EXPECT_TRUE(effects.publish_status);
  EXPECT_TRUE(effects.redraw);
}

}  // namespace
