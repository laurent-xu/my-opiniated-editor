#include <poll.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/base/file_descriptor.h"
#include "src/base/terminal_size.h"
#include "src/parent/overlay/overlay_footer.h"
#include "src/parent/test/support/test_paths.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_preview_request.h"
#include "src/parent/tray/tray_snapshot.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"
#include "src/parent/worktree/overlay/worktree_management_overlay_test_support.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace {

using moe::parent::test_support::required_environment_path;
using moe::parent::test_support::runfile_path;
using moe::parent::test_support::save_empty_worktree_registry;
using moe::parent::test_support::used_anonymous_tray;
using moe::parent::test_support::worktree_overlay_mode_labels;

TEST(WorktreeManagementOverlayTest, AddWorktreeModeShowsAllRepositoriesInPathPicker) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "repository-picker";
  std::filesystem::path const first_repository = root / "first-repository";
  std::filesystem::path const second_repository = root / "second-repository";
  std::filesystem::create_directories(first_repository);
  std::filesystem::create_directories(second_repository);
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};

  moe::parent::persistence::WorktreeRegistry registry =
      moe::parent::WorktreeRegistryStore::empty_registry();
  registry.add_repositories()->set_root_path(first_repository.string());
  registry.add_repositories()->set_root_path(second_repository.string());
  moe::parent::WorktreeRegistryStore(registry_path).save(registry);

  std::unique_ptr<moe::parent::WorktreeManagementOverlay> overlay =
      moe::parent::WorktreeManagementOverlay::start(
          "/unused/workspace_parent", registry_path, root, "/unused/git",
          runfile_path("test/fixtures/fake_fzf").string(), {}, size);
  overlay->write_input("\t");
  EXPECT_TRUE(overlay->take_full_redraw_request());
  EXPECT_EQ(overlay->opaque_region_start_row(), 12);

  std::optional<moe::base::FileDescriptor> const descriptor = overlay->process_file_descriptor();
  if (!descriptor.has_value()) {
    ADD_FAILURE() << "repository picker did not expose its PTY descriptor";
    return;
  }
  pollfd readable{.fd = descriptor->value(), .events = POLLIN, .revents = 0};
  ASSERT_EQ(::poll(&readable, 1, 1000), 1);
  EXPECT_TRUE(overlay->read_process_output());

  std::string const redraw = overlay->redraw_output();
  std::size_t const first_repository_position = redraw.find(first_repository.filename().string());
  std::size_t const second_repository_position = redraw.find(second_repository.filename().string());
  ASSERT_NE(first_repository_position, std::string::npos);
  ASSERT_NE(second_repository_position, std::string::npos);
  EXPECT_EQ(redraw.find("[Add worktree]"), std::string::npos);
  EXPECT_TRUE(redraw.starts_with(
      moe::parent::render_overlay_footer(worktree_overlay_mode_labels(), 1, size)));

  overlay->write_input("\t");
  EXPECT_TRUE(overlay->redraw_output().starts_with(
      moe::parent::render_overlay_footer(worktree_overlay_mode_labels(), 2, size)));
}

TEST(WorktreeManagementOverlayTest, WorktreePickerIncludesAndPreviewsUsedAnonymousTray) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "anonymous-picker";
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};
  save_empty_worktree_registry(registry_path);
  std::vector<moe::parent::TraySnapshot> const session_trays = used_anonymous_tray(root);

  std::unique_ptr<moe::parent::WorktreeManagementOverlay> overlay =
      moe::parent::WorktreeManagementOverlay::start(
          "/unused/workspace_parent", registry_path, root, "/unused/git",
          runfile_path("test/fixtures/fake_fzf").string(), session_trays, size);
  std::optional<moe::parent::TrayPreviewRequest> const preview = overlay->preview_request();
  if (!preview.has_value()) {
    ADD_FAILURE() << "worktree picker did not expose a tray preview";
    return;
  }
  moe::parent::TrayPreviewRequest const& preview_request = *preview;
  EXPECT_EQ(preview_request.tray_id,
            moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()));
  EXPECT_EQ(preview_request.origin.row, 0);
  EXPECT_EQ(preview_request.origin.column, 0);
  EXPECT_EQ(preview_request.size.rows, 12);
  EXPECT_EQ(preview_request.size.cols, 100);
  EXPECT_EQ(overlay->opaque_region_start_row(), 12);

  std::optional<moe::base::FileDescriptor> const descriptor = overlay->process_file_descriptor();
  if (!descriptor.has_value()) {
    ADD_FAILURE() << "worktree picker did not expose its PTY descriptor";
    return;
  }
  pollfd readable{.fd = descriptor->value(), .events = POLLIN, .revents = 0};
  ASSERT_EQ(::poll(&readable, 1, 1000), 1);
  EXPECT_TRUE(overlay->read_process_output());
  EXPECT_NE(overlay->redraw_output().find("/anonymous/1"), std::string::npos);
}

}  // namespace
