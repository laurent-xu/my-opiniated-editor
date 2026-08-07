#include <memory>
#include <optional>
#include <stdexcept>
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

template <typename Value>
Value required(std::optional<Value> const& value) {
  if (!value.has_value()) {
    throw std::logic_error("expected a value");
  }
  return value.value();
}

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

TEST(TrayManagerTest, SelectionMovesOnlyBetweenCompleteNodesAtOneLevel) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::PaneId const pane_a = manager->active_focused_pane_id();
  moe::parent::PaneId const pane_b = manager->split_active_focused_pane(
      moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneId const pane_c = manager->split_active_focused_pane(
      moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneLayout const& layout = manager->active_pane_layout();
  moe::parent::PaneNodeId const node_a = required(layout.find_pane(pane_a));
  moe::parent::PaneNodeId const node_b = required(layout.find_pane(pane_b));
  moe::parent::PaneNodeId const node_c = required(layout.find_pane(pane_c));
  moe::parent::PaneNodeId const group_bc = required(layout.node(node_b).parent());
  ASSERT_TRUE(manager->focus_active_pane(pane_b));

  ASSERT_TRUE(manager->toggle_active_pane_selection());
  ASSERT_TRUE(manager->active_pane_selection().has_value());
  moe::parent::PaneSelection const selected_b = required(manager->active_pane_selection());
  EXPECT_EQ(selected_b.nodes(), (std::vector<moe::parent::PaneNodeId>{node_b}));
  EXPECT_NE(manager->active_redraw_output().find("\x1b[0;48;5;33m"), std::string::npos);

  ASSERT_TRUE(manager->promote_active_pane_selection());
  moe::parent::PaneSelection const selected_group = required(manager->active_pane_selection());
  EXPECT_EQ(selected_group.nodes(), (std::vector<moe::parent::PaneNodeId>{group_bc}));
  ASSERT_TRUE(manager->step_active_pane_selection(moe::parent::PaneFocusDirection::LEFT));
  moe::parent::PaneSelection const selected_root_range = required(manager->active_pane_selection());
  EXPECT_EQ(selected_root_range.nodes(), (std::vector<moe::parent::PaneNodeId>{node_a, group_bc}));
  EXPECT_FALSE(selected_root_range.contains(node_b));
  EXPECT_FALSE(selected_root_range.contains(node_c));

  ASSERT_TRUE(manager->step_active_pane_selection(moe::parent::PaneFocusDirection::RIGHT));
  ASSERT_TRUE(manager->descend_active_pane_selection());
  moe::parent::PaneSelection const selected_child = required(manager->active_pane_selection());
  EXPECT_EQ(selected_child.nodes(), (std::vector<moe::parent::PaneNodeId>{node_b}));

  ASSERT_TRUE(manager->toggle_active_pane_selection());
  EXPECT_FALSE(manager->active_pane_selection().has_value());
}

TEST(TrayManagerTest, ResizeAndEqualizeUseSelectionOrFocusedPane) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::PaneId const first_pane = manager->active_focused_pane_id();
  static_cast<void>(manager->split_active_focused_pane(moe::parent::PaneSplitAxis::TOP_TO_BOTTOM,
                                                       moe::parent::PaneInsertion::AFTER));
  ASSERT_TRUE(manager->focus_active_pane(first_pane));

  ASSERT_TRUE(manager->resize_active_pane_selection(10));
  moe::parent::PaneSplit const& resized =
      manager->active_pane_layout().node(manager->active_pane_layout().root_id()).split();
  EXPECT_EQ(resized.children[0].percentage.value(), 60);
  EXPECT_EQ(resized.children[1].percentage.value(), 40);

  ASSERT_TRUE(manager->equalize_active_pane_selection());
  moe::parent::PaneSplit const& equalized =
      manager->active_pane_layout().node(manager->active_pane_layout().root_id()).split();
  EXPECT_EQ(equalized.children[0].percentage.value(), 50);
  EXPECT_EQ(equalized.children[1].percentage.value(), 50);

  ASSERT_TRUE(manager->toggle_active_pane_selection());
  ASSERT_TRUE(manager->step_active_pane_selection(moe::parent::PaneFocusDirection::DOWN));
  EXPECT_FALSE(manager->resize_active_pane_selection(10));
}

TEST(TrayManagerTest, EqualizeKeepsUnselectedSiblingPercentage) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  static_cast<void>(manager->split_active_focused_pane(moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                                       moe::parent::PaneInsertion::AFTER));
  moe::parent::PaneId const second_pane = manager->active_focused_pane_id();
  static_cast<void>(manager->split_active_focused_pane(moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                                       moe::parent::PaneInsertion::AFTER));
  ASSERT_TRUE(manager->focus_active_pane(second_pane));
  ASSERT_TRUE(manager->resize_active_pane_selection(5));

  ASSERT_TRUE(manager->toggle_active_pane_selection());
  ASSERT_TRUE(manager->step_active_pane_selection(moe::parent::PaneFocusDirection::RIGHT));
  ASSERT_TRUE(manager->equalize_active_pane_selection());

  moe::parent::PaneSplit const& equalized =
      manager->active_pane_layout().node(manager->active_pane_layout().root_id()).split();
  EXPECT_EQ(equalized.children[0].percentage.value(), 47);
  EXPECT_EQ(equalized.children[1].percentage.value(), 27);
  EXPECT_EQ(equalized.children[2].percentage.value(), 26);
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
