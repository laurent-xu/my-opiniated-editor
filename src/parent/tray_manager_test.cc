#include "src/parent/tray_manager.h"

#include <poll.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/workspace_parent.h"

namespace {

using namespace std::chrono_literals;

std::filesystem::path required_env_path(char const* name) {
  char const* value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error(std::string("missing environment variable: ") + name);
  }
  return {value};
}

std::unique_ptr<moe::parent::TrayManager> start_manager() {
  return moe::parent::TrayManager::start(moe::parent::TrayConfig{
      .command = moe::parent::interactive_shell_command(moe::parent::configured_login_shell()),
      .working_directory = required_env_path("TEST_TMPDIR"),
      .initial_size = {.rows = 24, .cols = 80},
  });
}

std::filesystem::path create_fake_worktree(std::string const& name) {
  std::filesystem::path const root = required_env_path("TEST_TMPDIR") / name;
  std::filesystem::create_directories(root / ".git");
  return std::filesystem::weakly_canonical(root);
}

moe::parent::TrayNumber required_tray_number(int const value) {
  std::optional<moe::parent::TrayNumber> const number = moe::parent::TrayNumber::from_int(value);
  if (number.has_value()) {
    return *number;
  }
  throw std::runtime_error("invalid tray number in test: " + std::to_string(value));
}

std::string shell_marker_command(std::string const& marker) {
  std::ostringstream command;
  command << "printf '";
  for (unsigned char const character : marker) {
    command << '\\' << std::oct << std::setw(3) << std::setfill('0') << static_cast<int>(character);
  }
  command << "\\012'\n";
  return command.str();
}

std::string read_until(moe::parent::TrayManager& manager, std::string const& needle) {
  std::string output;
  auto const deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd descriptor{
        .fd = manager.active_content_file_descriptor().value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, 100);
    if (result <= 0 || (descriptor.revents & POLLIN) == 0) {
      continue;
    }

    std::optional<std::string> chunk = manager.read_active_output();
    if (!chunk.has_value()) {
      continue;
    }
    output.append(*chunk);
    if (output.find(needle) != std::string::npos) {
      return output;
    }
  }

  throw std::runtime_error("timed out waiting for '" + needle + "'; output was: " + output);
}

bool process_exists(moe::base::ProcessId const process_id) {
  return ::kill(process_id.value(), 0) == 0;
}

bool wait_for_exited_tray(moe::parent::TrayManager& manager) {
  auto const deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (manager.destroy_exited_trays()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

TEST(TrayManagerTest, StartsWithAnonymousTrayOne) {
  std::unique_ptr<moe::parent::TrayManager> manager = start_manager();

  moe::parent::TraySnapshot const active = manager->active_snapshot();
  EXPECT_EQ(active.id.key(), "anonymous:1");
  EXPECT_EQ(active.id.label(), "tray 1");
  EXPECT_EQ(active.id.anonymous_number().value(), 1);
  EXPECT_EQ(active.label, "tray 1");
  EXPECT_EQ(active.working_directory, required_env_path("TEST_TMPDIR"));
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
  std::filesystem::path const path = required_env_path("TEST_TMPDIR") / "not-a-worktree";
  std::filesystem::create_directories(path);

  EXPECT_THROW(static_cast<void>(manager->switch_to_worktree(path)), std::invalid_argument);
}

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
