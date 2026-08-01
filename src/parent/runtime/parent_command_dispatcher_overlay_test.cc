#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "src/parent/input/command/navigate_overlay_command.h"
#include "src/parent/input/command/overlay_navigation.h"
#include "src/parent/input/command/toggle_command_mode_command.h"
#include "src/parent/input/command/toggle_worktree_overlay_command.h"
#include "src/parent/overlay/overlay_footer.h"
#include "src/parent/runtime/parent_command_dispatcher.h"
#include "src/parent/runtime/parent_command_dispatcher_test_support.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/tray/tray_manager_test_support.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"

namespace {

using moe::parent::test_support::command_dispatcher_test_config;
using moe::parent::test_support::COMMAND_DISPATCHER_TEST_SIZE;

constexpr std::array<std::string_view, 3> MODE_LABELS{
    "Worktrees",
    "Add worktree",
    "Add repository",
};

TEST(ParentCommandDispatcherOverlayTest, NavigationRequiresCommandModeAndThenReachesOverlay) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(
      *trays, command_dispatcher_test_config("overlay-navigation"));
  static_cast<void>(dispatcher.dispatch(moe::parent::ToggleWorktreeOverlayCommand{},
                                        COMMAND_DISPATCHER_TEST_SIZE));
  moe::parent::WorktreeManagementOverlay* const overlay =
      trays->active_worktree_management_overlay();
  ASSERT_NE(overlay, nullptr);

  std::string const initial_footer =
      moe::parent::render_overlay_footer(MODE_LABELS, 0, COMMAND_DISPATCHER_TEST_SIZE);
  std::string const worktree_footer =
      moe::parent::render_overlay_footer(MODE_LABELS, 1, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_TRUE(overlay->redraw_output().starts_with(initial_footer));

  moe::parent::NavigateOverlayCommand const tab{
      .navigation = moe::parent::OverlayNavigation::TAB,
  };
  moe::parent::ParentCommandDispatchEffects const ignored =
      dispatcher.dispatch(tab, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_TRUE(overlay->redraw_output().starts_with(initial_footer));
  EXPECT_FALSE(ignored.publish_status);
  EXPECT_FALSE(ignored.redraw);

  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));
  moe::parent::ParentCommandDispatchEffects const forwarded =
      dispatcher.dispatch(tab, COMMAND_DISPATCHER_TEST_SIZE);
  EXPECT_TRUE(overlay->redraw_output().starts_with(worktree_footer));
  EXPECT_FALSE(forwarded.publish_status);
  EXPECT_FALSE(forwarded.redraw);
}

TEST(ParentCommandDispatcherOverlayTest, EnterCancelsPendingActionWithoutDestroyingTray) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcher dispatcher(*trays,
                                                  command_dispatcher_test_config("overlay-enter"));
  static_cast<void>(dispatcher.dispatch(moe::parent::ToggleWorktreeOverlayCommand{},
                                        COMMAND_DISPATCHER_TEST_SIZE));
  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));
  moe::parent::WorktreeManagementOverlay* const overlay =
      trays->active_worktree_management_overlay();
  ASSERT_NE(overlay, nullptr);
  ASSERT_TRUE(overlay->begin_tray_action_confirmation(moe::parent::TrayActionKind::CLEAR));

  moe::parent::ParentCommandDispatchEffects const effects = dispatcher.dispatch(
      moe::parent::NavigateOverlayCommand{.navigation = moe::parent::OverlayNavigation::ENTER},
      COMMAND_DISPATCHER_TEST_SIZE);

  EXPECT_FALSE(overlay->has_tray_action_confirmation());
  EXPECT_TRUE(effects.publish_status);
  EXPECT_TRUE(effects.redraw);
  EXPECT_FALSE(effects.trays_destroyed);
}

}  // namespace
