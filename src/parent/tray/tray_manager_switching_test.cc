#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/tray/tray_manager_test_support.h"

namespace {

using moe::parent::test_support::create_fake_worktree;
using moe::parent::test_support::read_until;
using moe::parent::test_support::required_environment_path;
using moe::parent::test_support::required_tray_number;
using moe::parent::test_support::shell_marker_command;
using moe::parent::test_support::start_manager;

TEST(TrayManagerTest, SwitchingCreatesLazyAnonymousTrayAndPreservesActiveTray) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();

  moe::parent::TraySnapshot const tray_one = manager->active_snapshot();
  moe::parent::TrayNumber const tray_two_number = required_tray_number(2);

  moe::parent::TraySnapshot const tray_two = manager->switch_to(tray_two_number);
  EXPECT_EQ(tray_two.id.key(), "anonymous:2");
  EXPECT_EQ(tray_two.id.label(), "tray 2");
  EXPECT_NE(tray_two.child_pid.value(), tray_one.child_pid.value());
  EXPECT_EQ(manager->active_snapshot().id.key(), "anonymous:2");

  std::vector<moe::parent::TraySnapshot> const snapshots = manager->tray_snapshots();
  ASSERT_EQ(snapshots.size(), 2);
  EXPECT_EQ(snapshots[0].id.key(), "anonymous:1");
  EXPECT_EQ(snapshots[1].id.key(), "anonymous:2");

  moe::parent::TraySnapshot const tray_one_again =
      manager->switch_to(moe::parent::TrayNumber::one());
  EXPECT_EQ(tray_one_again.child_pid.value(), tray_one.child_pid.value());
  EXPECT_EQ(manager->active_snapshot().id.key(), "anonymous:1");
}

TEST(TrayManagerTest, SwitchingBackPreservesShellState) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::TrayNumber const tray_two = required_tray_number(2);

  manager->write_input("export MOE_TRAY_MARKER=tray_one\n");
  manager->write_input(shell_marker_command("__moe_export_done__"));
  static_cast<void>(read_until(*manager, "__moe_export_done__"));

  static_cast<void>(manager->switch_to(tray_two));
  manager->write_input("printf '__moe_tray2_%s__\\n' \"${MOE_TRAY_MARKER:-empty}\"\n");
  std::string const tray_two_output = read_until(*manager, "__moe_tray2_empty__");
  EXPECT_NE(tray_two_output.find("__moe_tray2_empty__"), std::string::npos);

  static_cast<void>(manager->switch_to(moe::parent::TrayNumber::one()));
  EXPECT_NE(manager->active_redraw_output().find("__moe_export_done__"), std::string::npos);
  manager->write_input("printf '__moe_tray1_%s__\\n' \"$MOE_TRAY_MARKER\"\n");
  std::string const tray_one_output = read_until(*manager, "__moe_tray1_tray_one__");
  EXPECT_NE(tray_one_output.find("__moe_tray1_tray_one__"), std::string::npos);
}

TEST(TrayManagerTest, WorktreeTrayStartsShellInWorktreeRoot) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  std::filesystem::path const root = create_fake_worktree("cwd-worktree");

  moe::parent::TraySnapshot const snapshot = manager->switch_to_worktree(root);
  EXPECT_EQ(snapshot.id.kind(), moe::parent::TrayIdKind::WORKTREE);
  EXPECT_EQ(snapshot.id.worktree_root(), root);
  EXPECT_EQ(snapshot.id.key(), "worktree:" + root.string());
  EXPECT_EQ(snapshot.label, "worktree " + root.string());
  EXPECT_EQ(snapshot.working_directory, root);

  manager->write_input("pwd\n");
  manager->write_input(shell_marker_command("__moe_worktree_pwd_done__"));

  std::string const output = read_until(*manager, "__moe_worktree_pwd_done__");
  EXPECT_NE(output.find(root.string()), std::string::npos);
}

TEST(TrayManagerTest, SwitchingToExistingWorktreeJumpsToStableTray) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  std::filesystem::path const root = create_fake_worktree("stable-worktree");
  std::filesystem::path const nested = root / "nested";
  std::filesystem::create_directories(nested);

  moe::parent::TraySnapshot const first = manager->switch_to_worktree(nested);
  static_cast<void>(manager->switch_to(required_tray_number(2)));
  moe::parent::TraySnapshot const second = manager->switch_to_worktree(root);

  EXPECT_EQ(second.id, first.id);
  EXPECT_EQ(second.child_pid.value(), first.child_pid.value());
  EXPECT_EQ(manager->active_snapshot().id, first.id);
}

TEST(TrayManagerTest, RejectsPathOutsideWorktree) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  std::filesystem::path const path = required_environment_path("TEST_TMPDIR") / "not-a-worktree";
  std::filesystem::create_directories(path);

  EXPECT_THROW(static_cast<void>(manager->switch_to_worktree(path)), std::invalid_argument);
}

}  // namespace
