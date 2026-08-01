#include <memory>

#include "gtest/gtest.h"
#include "src/parent/input/begin_tray_action_command.h"
#include "src/parent/input/confirmation_decision.h"
#include "src/parent/input/resolve_tray_action_command.h"
#include "src/parent/input/switch_anonymous_tray_command.h"
#include "src/parent/input/toggle_command_mode_command.h"
#include "src/parent/input/toggle_worktree_overlay_command.h"
#include "src/parent/input/tray_action_intent.h"
#include "src/parent/runtime/parent_command_dispatcher.h"
#include "src/parent/runtime/parent_command_dispatcher_test_support.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/tray/tray_manager_test_support.h"

namespace {

using moe::parent::test_support::command_dispatcher_test_config;
using moe::parent::test_support::COMMAND_DISPATCHER_TEST_SIZE;

void open_overlay_in_command_mode(moe::parent::ParentCommandDispatcher& dispatcher) {
  static_cast<void>(dispatcher.dispatch(moe::parent::ToggleWorktreeOverlayCommand{},
                                        COMMAND_DISPATCHER_TEST_SIZE));
  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));
}

TEST(ParentCommandDispatcherTrayTest, SwitchesAnonymousTrayAndRequestsSurfaceRefresh) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(
      *trays, command_dispatcher_test_config("switch-anonymous"));
  moe::parent::TrayNumber const tray_two = moe::parent::test_support::required_tray_number(2);

  moe::parent::ParentCommandDispatchEffects const effects =
      dispatcher.dispatch(moe::parent::SwitchAnonymousTrayCommand{.tray_number = tray_two},
                          COMMAND_DISPATCHER_TEST_SIZE);

  EXPECT_EQ(trays->active_id(), moe::parent::TrayId::anonymous(tray_two));
  EXPECT_TRUE(effects.publish_status);
  EXPECT_TRUE(effects.redraw);
  EXPECT_FALSE(effects.trays_destroyed);
}

TEST(ParentCommandDispatcherTrayTest, ConfirmingClearRecreatesDestroyedActiveTrayOne) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(
      *trays, command_dispatcher_test_config("clear-active-anonymous"));
  moe::base::ProcessId const original_pid = trays->active_snapshot().child_pid;
  open_overlay_in_command_mode(dispatcher);
  static_cast<void>(dispatcher.dispatch(
      moe::parent::BeginTrayActionCommand{.action = moe::parent::TrayActionIntent::CLEAR},
      COMMAND_DISPATCHER_TEST_SIZE));

  moe::parent::ParentCommandDispatchEffects const effects = dispatcher.dispatch(
      moe::parent::ResolveTrayActionCommand{
          .decision = moe::parent::ConfirmationDecision::CONFIRM,
      },
      COMMAND_DISPATCHER_TEST_SIZE);

  EXPECT_EQ(trays->active_id(), moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()));
  EXPECT_NE(trays->active_snapshot().child_pid.value(), original_pid.value());
  EXPECT_TRUE(effects.publish_status);
  EXPECT_TRUE(effects.redraw);
  EXPECT_TRUE(effects.trays_destroyed);
}

TEST(ParentCommandDispatcherTrayTest, CancelingClearPreservesActiveTray) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(
      *trays, command_dispatcher_test_config("cancel-active-clear"));
  moe::base::ProcessId const original_pid = trays->active_snapshot().child_pid;
  open_overlay_in_command_mode(dispatcher);
  static_cast<void>(dispatcher.dispatch(
      moe::parent::BeginTrayActionCommand{.action = moe::parent::TrayActionIntent::CLEAR},
      COMMAND_DISPATCHER_TEST_SIZE));

  moe::parent::ParentCommandDispatchEffects const effects = dispatcher.dispatch(
      moe::parent::ResolveTrayActionCommand{
          .decision = moe::parent::ConfirmationDecision::CANCEL,
      },
      COMMAND_DISPATCHER_TEST_SIZE);

  EXPECT_EQ(trays->active_snapshot().child_pid.value(), original_pid.value());
  EXPECT_TRUE(effects.publish_status);
  EXPECT_TRUE(effects.redraw);
  EXPECT_FALSE(effects.trays_destroyed);
}

}  // namespace
