#include <poll.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/base/file_descriptor.h"
#include "src/base/terminal_size.h"
#include "src/parent/test/support/test_paths.h"
#include "src/parent/tray/tray_action_kind.h"
#include "src/parent/tray/tray_action_request.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/tray/tray_snapshot.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"
#include "src/parent/worktree/overlay/worktree_management_overlay_test_support.h"
#include "src/parent/worktree/worktree_registry_store.h"

namespace {

using moe::parent::test_support::required_environment_path;
using moe::parent::test_support::runfile_path;
using moe::parent::test_support::save_empty_worktree_registry;
using moe::parent::test_support::used_anonymous_tray;

TEST(WorktreeManagementOverlayTest, ConfirmsRemovalOfHighlightedWorktreePickerTray) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "remove-picker";
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};
  save_empty_worktree_registry(registry_path);
  std::vector<moe::parent::TraySnapshot> const session_trays = used_anonymous_tray(root);
  std::unique_ptr<moe::parent::WorktreeManagementOverlay> overlay =
      moe::parent::WorktreeManagementOverlay::start(
          "/unused/workspace_parent", registry_path, root, "/unused/git",
          runfile_path("test/fixtures/fake_fzf").string(), session_trays, size);

  ASSERT_TRUE(overlay->begin_tray_action_confirmation(moe::parent::TrayActionKind::REMOVE));
  EXPECT_TRUE(overlay->has_tray_action_confirmation());
  EXPECT_NE(overlay->redraw_output().find("Remove /anonymous/1? [y/N]"), std::string::npos);

  std::optional<moe::parent::TrayActionRequest> const confirmed =
      overlay->resolve_tray_action_confirmation(true);
  if (!confirmed.has_value()) {
    ADD_FAILURE() << "confirmed removal did not return a tray";
    return;
  }
  EXPECT_EQ(confirmed->kind, moe::parent::TrayActionKind::REMOVE);
  EXPECT_EQ(confirmed->tray_id, moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()));
  EXPECT_FALSE(overlay->has_tray_action_confirmation());
}

TEST(WorktreeManagementOverlayTest, CancelsRemovalAndRejectsItOutsideWorktreePicker) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "cancel-remove";
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};
  save_empty_worktree_registry(registry_path);
  std::vector<moe::parent::TraySnapshot> const session_trays = used_anonymous_tray(root);
  std::unique_ptr<moe::parent::WorktreeManagementOverlay> overlay =
      moe::parent::WorktreeManagementOverlay::start(
          "/unused/workspace_parent", registry_path, root, "/unused/git",
          runfile_path("test/fixtures/fake_fzf").string(), session_trays, size);

  ASSERT_TRUE(overlay->begin_tray_action_confirmation(moe::parent::TrayActionKind::CLEAR));
  EXPECT_NE(overlay->redraw_output().find("Clear /anonymous/1? [y/N]"), std::string::npos);
  EXPECT_FALSE(overlay->resolve_tray_action_confirmation(false).has_value());
  EXPECT_FALSE(overlay->has_tray_action_confirmation());

  overlay->write_input("\t");
  EXPECT_FALSE(overlay->begin_tray_action_confirmation(moe::parent::TrayActionKind::REMOVE));
}

TEST(WorktreeManagementOverlayTest, MissingTrackedWorktreeRemainsSelectableForRemoval) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "missing-picker";
  std::filesystem::path const repository = root / "repository";
  std::filesystem::path const missing_worktree = repository / "missing";
  std::filesystem::create_directories(repository / ".bare");
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::parent::persistence::WorktreeRegistry registry =
      moe::parent::WorktreeRegistryStore::empty_registry();
  moe::parent::persistence::Repository* const entry = registry.add_repositories();
  entry->set_root_path(std::filesystem::weakly_canonical(repository).string());
  entry->add_worktrees()->set_path(std::filesystem::weakly_canonical(missing_worktree).string());
  moe::parent::WorktreeRegistryStore(registry_path).save(registry);
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};

  std::unique_ptr<moe::parent::WorktreeManagementOverlay> overlay =
      moe::parent::WorktreeManagementOverlay::start("/unused/workspace_parent", registry_path, root,
                                                    runfile_path("test/fixtures/fake_git").string(),
                                                    runfile_path("test/fixtures/fake_fzf").string(),
                                                    {}, size);
  std::optional<moe::base::FileDescriptor> const descriptor = overlay->process_file_descriptor();
  if (!descriptor.has_value()) {
    ADD_FAILURE() << "missing worktree picker did not expose its PTY descriptor";
    return;
  }
  pollfd readable{.fd = descriptor.value().value(), .events = POLLIN, .revents = 0};
  ASSERT_EQ(::poll(&readable, 1, 1000), 1);
  EXPECT_TRUE(overlay->read_process_output());

  EXPECT_NE(overlay->redraw_output().find("[unavailable]"), std::string::npos);
  ASSERT_TRUE(overlay->begin_tray_action_confirmation(moe::parent::TrayActionKind::REMOVE));
  std::optional<moe::parent::TrayActionRequest> const request =
      overlay->resolve_tray_action_confirmation(true);
  if (!request.has_value()) {
    ADD_FAILURE() << "missing worktree removal did not return a tray";
    return;
  }
  EXPECT_EQ(request->kind, moe::parent::TrayActionKind::REMOVE);
  EXPECT_EQ(request->tray_id.kind(), moe::parent::TrayIdKind::WORKTREE);
  EXPECT_EQ(request->tray_id.worktree_root(), std::filesystem::weakly_canonical(missing_worktree));
}

}  // namespace
