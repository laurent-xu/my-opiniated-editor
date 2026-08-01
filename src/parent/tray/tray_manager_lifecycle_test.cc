#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/tray/tray_manager_test_support.h"

namespace {

using moe::parent::test_support::create_fake_worktree;
using moe::parent::test_support::process_exists;
using moe::parent::test_support::read_until;
using moe::parent::test_support::required_tray_number;
using moe::parent::test_support::shell_marker_command;
using moe::parent::test_support::start_manager;
using moe::parent::test_support::wait_for_exited_tray;

TEST(TrayManagerTest, DestroyingInactiveAnonymousTrayClosesItsShell) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::TrayNumber const tray_two = required_tray_number(2);
  moe::parent::TraySnapshot const second = manager->switch_to(tray_two);
  static_cast<void>(manager->switch_to(moe::parent::TrayNumber::one()));

  EXPECT_TRUE(manager->destroy_tray(second.id));

  EXPECT_FALSE(process_exists(second.child_pid));
  EXPECT_EQ(manager->active_id(), moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()));
  EXPECT_EQ(manager->tray_snapshots().size(), 1U);
}

TEST(TrayManagerTest, DestroyingActiveTrayFallsBackToExistingAnonymousTrayOne) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::TraySnapshot const first = manager->active_snapshot();
  moe::parent::TraySnapshot const second = manager->switch_to(required_tray_number(2));

  EXPECT_TRUE(manager->destroy_tray(second.id));

  EXPECT_FALSE(process_exists(second.child_pid));
  EXPECT_EQ(manager->active_snapshot().id, first.id);
  EXPECT_EQ(manager->active_snapshot().child_pid.value(), first.child_pid.value());
}

TEST(TrayManagerTest, DestroyingInactiveWorktreeTrayClosesItsShell) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  std::filesystem::path const root = create_fake_worktree("destroy-inactive-worktree");
  moe::parent::TraySnapshot const worktree = manager->switch_to_worktree(root);
  static_cast<void>(manager->switch_to(moe::parent::TrayNumber::one()));

  EXPECT_TRUE(manager->destroy_tray(worktree.id));

  EXPECT_FALSE(process_exists(worktree.child_pid));
  EXPECT_EQ(manager->active_id(), moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()));
  EXPECT_EQ(manager->tray_snapshots().size(), 1U);
}

TEST(TrayManagerTest, DestroyingAnonymousTrayOneRecreatesIt) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::TraySnapshot const first = manager->active_snapshot();

  EXPECT_TRUE(manager->destroy_tray(first.id));

  moe::parent::TraySnapshot const replacement = manager->active_snapshot();
  EXPECT_EQ(replacement.id, first.id);
  EXPECT_NE(replacement.child_pid.value(), first.child_pid.value());
  EXPECT_FALSE(process_exists(first.child_pid));
}

TEST(TrayManagerTest, DestroyingInactiveAnonymousTrayOneDoesNotRecreateIt) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::TraySnapshot const first = manager->active_snapshot();
  moe::parent::TraySnapshot const second = manager->switch_to(required_tray_number(2));

  EXPECT_TRUE(manager->destroy_tray(first.id));

  EXPECT_EQ(manager->active_snapshot().id, second.id);
  EXPECT_EQ(manager->active_snapshot().child_pid.value(), second.child_pid.value());
  std::vector<moe::parent::TraySnapshot> const snapshots = manager->tray_snapshots();
  ASSERT_EQ(snapshots.size(), 1U);
  EXPECT_EQ(snapshots.front().id, second.id);
  EXPECT_FALSE(process_exists(first.child_pid));
}

TEST(TrayManagerTest, ExitedShellDestroysTrayAndRecreatesAnonymousTrayOne) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::TraySnapshot const first = manager->active_snapshot();
  manager->write_input("exit\n");

  ASSERT_TRUE(wait_for_exited_tray(*manager));

  moe::parent::TraySnapshot const replacement = manager->active_snapshot();
  EXPECT_EQ(replacement.id, first.id);
  EXPECT_NE(replacement.child_pid.value(), first.child_pid.value());
  manager->write_input(shell_marker_command("__moe_recreated_tray_ready__"));
  EXPECT_NE(
      read_until(*manager, "__moe_recreated_tray_ready__").find("__moe_recreated_tray_ready__"),
      std::string::npos);
}

TEST(TrayManagerTest, ExitedWorktreeShellFallsBackToAnonymousTrayOne) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();
  moe::parent::TraySnapshot const first = manager->active_snapshot();
  std::filesystem::path const root = create_fake_worktree("exited-worktree");
  moe::parent::TraySnapshot const worktree = manager->switch_to_worktree(root);
  manager->write_input("exit\n");

  ASSERT_TRUE(wait_for_exited_tray(*manager));

  EXPECT_EQ(manager->active_snapshot().id, first.id);
  EXPECT_EQ(manager->active_snapshot().child_pid.value(), first.child_pid.value());
  EXPECT_FALSE(process_exists(worktree.child_pid));
}

}  // namespace
