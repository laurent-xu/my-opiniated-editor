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

  std::vector<moe::parent::TrayOutputSource> const sources = manager->output_sources();
  ASSERT_EQ(sources.size(), 2);
  EXPECT_EQ(sources[0].tray_id.key(), "anonymous:1");
  EXPECT_TRUE(sources[0].file_descriptor.is_valid());
  EXPECT_EQ(sources[1].tray_id.key(), "anonymous:2");
  EXPECT_TRUE(sources[1].file_descriptor.is_valid());
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
