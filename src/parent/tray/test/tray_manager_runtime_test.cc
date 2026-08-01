#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/base/terminal_size.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/tray/tray_manager_test_support.h"

namespace {

using moe::parent::test_support::read_until;
using moe::parent::test_support::required_environment_path;
using moe::parent::test_support::required_tray_number;
using moe::parent::test_support::shell_marker_command;
using moe::parent::test_support::start_manager;

TEST(TrayManagerTest, StartsWithAnonymousTrayOne) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();

  moe::parent::TraySnapshot const active = manager->active_snapshot();
  EXPECT_EQ(active.id.key(), "anonymous:1");
  EXPECT_EQ(active.id.label(), "tray 1");
  EXPECT_EQ(active.id.anonymous_number().value(), 1);
  EXPECT_EQ(active.label, "tray 1");
  EXPECT_EQ(active.working_directory, required_environment_path("TEST_TMPDIR"));
  EXPECT_GT(active.child_pid.value(), 0);
  EXPECT_TRUE(manager->active_content_file_descriptor().is_valid());

  std::vector<moe::parent::TraySnapshot> const snapshots = manager->tray_snapshots();
  ASSERT_EQ(snapshots.size(), 1);
  EXPECT_EQ(snapshots.front().id.key(), "anonymous:1");
}

TEST(TrayManagerTest, RoutesInputAndOutputThroughActiveTray) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();

  manager->write_input(shell_marker_command("__moe_tray_one_output__"));
  std::string const output = read_until(*manager, "__moe_tray_one_output__");

  EXPECT_NE(output.find("__moe_tray_one_output__"), std::string::npos);
  EXPECT_NE(manager->active_redraw_output().find("__moe_tray_one_output__"), std::string::npos);
}

TEST(TrayManagerTest, ListsOutputSourcesForCreatedTrays) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  static_cast<void>(manager->switch_to(required_tray_number(2)));

  std::vector<moe::parent::TrayPaneOutputSource> const sources = manager->output_sources();
  ASSERT_EQ(sources.size(), 2);
  EXPECT_EQ(sources[0].tray_id.key(), "anonymous:1");
  EXPECT_TRUE(sources[0].file_descriptor.is_valid());
  EXPECT_EQ(sources[1].tray_id.key(), "anonymous:2");
  EXPECT_TRUE(sources[1].file_descriptor.is_valid());
}

TEST(TrayManagerTest, SplitCreatesIndependentPanesAndComposesTheirOutput) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::PaneId const first_pane = manager->active_focused_pane_id();
  moe::parent::PaneId const second_pane = manager->split_active_focused_pane(
      moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, moe::parent::PaneInsertion::AFTER);

  std::vector<moe::parent::TrayPaneOutputSource> const sources = manager->output_sources();
  ASSERT_EQ(sources.size(), 2U);
  EXPECT_EQ(sources[0].pane_id, first_pane);
  EXPECT_EQ(sources[1].pane_id, second_pane);
  EXPECT_EQ(manager->active_focused_pane_id(), second_pane);

  manager->write_input("export MOE_PANE_MARKER=pane_two\n");
  manager->write_input(shell_marker_command("__pane_two_ready__"));
  static_cast<void>(read_until(*manager, "__pane_two_ready__"));

  ASSERT_TRUE(manager->focus_active_pane(first_pane));
  manager->write_input("printf '__pane_one_%s__\\n' \"${MOE_PANE_MARKER:-empty}\"\n");
  std::string const first_output = read_until(*manager, "__pane_one_empty__");
  EXPECT_NE(first_output.find("__pane_one_empty__"), std::string::npos);

  std::string const redraw = manager->active_redraw_output();
  EXPECT_NE(redraw.find("__pane_one_empty__"), std::string::npos);
  EXPECT_NE(redraw.find("__pane_two_ready__"), std::string::npos);
}

TEST(TrayManagerTest, SplitResizesBothChildPtysToTheirRegions) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::PaneId const first_pane = manager->active_focused_pane_id();
  static_cast<void>(manager->split_active_focused_pane(moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                                       moe::parent::PaneInsertion::AFTER));

  manager->write_input("stty size\n");
  manager->write_input(shell_marker_command("__second_size_ready__"));
  std::string const second_output = read_until(*manager, "__second_size_ready__");
  EXPECT_NE(second_output.find("24 39"), std::string::npos);

  ASSERT_TRUE(manager->focus_active_pane(first_pane));
  manager->write_input("stty size\n");
  manager->write_input(shell_marker_command("__first_size_ready__"));
  std::string const first_output = read_until(*manager, "__first_size_ready__");
  EXPECT_NE(first_output.find("24 40"), std::string::npos);
}

TEST(TrayManagerTest, MovesFocusAcrossNestedPaneGeometry) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::PaneId const left = manager->active_focused_pane_id();
  moe::parent::PaneId const top_right = manager->split_active_focused_pane(
      moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneId const bottom_right = manager->split_active_focused_pane(
      moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, moe::parent::PaneInsertion::AFTER);
  ASSERT_TRUE(manager->focus_active_pane(left));

  EXPECT_TRUE(manager->focus_active_pane_direction(moe::parent::PaneFocusDirection::RIGHT));
  EXPECT_EQ(manager->active_focused_pane_id(), top_right);
  EXPECT_TRUE(manager->focus_active_pane_direction(moe::parent::PaneFocusDirection::DOWN));
  EXPECT_EQ(manager->active_focused_pane_id(), bottom_right);
  EXPECT_TRUE(manager->focus_active_pane_direction(moe::parent::PaneFocusDirection::LEFT));
  EXPECT_EQ(manager->active_focused_pane_id(), left);
  EXPECT_FALSE(manager->focus_active_pane_direction(moe::parent::PaneFocusDirection::LEFT));
}

TEST(TrayManagerTest, MaximizeIsTransientAndRestoresSplitPtySize) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  static_cast<void>(manager->split_active_focused_pane(moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                                       moe::parent::PaneInsertion::AFTER));

  ASSERT_TRUE(manager->toggle_active_focused_pane_maximized());
  EXPECT_TRUE(manager->active_focused_pane_is_maximized());
  manager->write_input("stty size\n");
  manager->write_input(shell_marker_command("__maximized_size_ready__"));
  std::string const maximized_output = read_until(*manager, "__maximized_size_ready__");
  EXPECT_NE(maximized_output.find("24 80"), std::string::npos);

  ASSERT_TRUE(manager->toggle_active_focused_pane_maximized());
  EXPECT_FALSE(manager->active_focused_pane_is_maximized());
  manager->write_input("stty size\n");
  manager->write_input(shell_marker_command("__restored_size_ready__"));
  std::string const restored_output = read_until(*manager, "__restored_size_ready__");
  EXPECT_NE(restored_output.find("24 39"), std::string::npos);
}

TEST(TrayManagerTest, MaximizeRendersOnlyFocusedPaneUntilRestored) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::PaneId const first_pane = manager->active_focused_pane_id();
  manager->write_input(shell_marker_command("__visible_first__"));
  static_cast<void>(read_until(*manager, "__visible_first__"));
  static_cast<void>(manager->split_active_focused_pane(moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                                       moe::parent::PaneInsertion::AFTER));
  manager->write_input(shell_marker_command("__visible_second__"));
  static_cast<void>(read_until(*manager, "__visible_second__"));

  ASSERT_TRUE(manager->toggle_active_focused_pane_maximized());
  std::string const maximized = manager->active_redraw_output();
  EXPECT_EQ(maximized.find("__visible_first__"), std::string::npos);
  EXPECT_NE(maximized.find("__visible_second__"), std::string::npos);

  ASSERT_TRUE(manager->focus_active_pane(first_pane));
  EXPECT_TRUE(manager->active_focused_pane_is_maximized());
  std::string const refocused = manager->active_redraw_output();
  EXPECT_NE(refocused.find("__visible_first__"), std::string::npos);
  EXPECT_EQ(refocused.find("__visible_second__"), std::string::npos);

  ASSERT_TRUE(manager->toggle_active_focused_pane_maximized());
  std::string const restored = manager->active_redraw_output();
  EXPECT_NE(restored.find("__visible_first__"), std::string::npos);
  EXPECT_NE(restored.find("__visible_second__"), std::string::npos);
}

TEST(TrayManagerTest, ResizeAppliesToActiveTray) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();

  manager->resize_active(moe::base::TerminalSize{.rows = 33, .cols = 111});
  manager->write_input("stty size\n");
  manager->write_input(shell_marker_command("__moe_resize_done__"));

  std::string const output = read_until(*manager, "__moe_resize_done__");
  EXPECT_NE(output.find("33 111"), std::string::npos);
}

TEST(TrayManagerTest, LazyTrayStartsAtLatestActiveSize) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::TrayNumber const tray_two = required_tray_number(2);

  manager->resize_active(moe::base::TerminalSize{.rows = 33, .cols = 111});
  static_cast<void>(manager->switch_to(tray_two));
  manager->write_input("stty size\n");
  manager->write_input(shell_marker_command("__moe_lazy_resize_done__"));

  std::string const output = read_until(*manager, "__moe_lazy_resize_done__");
  EXPECT_NE(output.find("33 111"), std::string::npos);
}

}  // namespace
