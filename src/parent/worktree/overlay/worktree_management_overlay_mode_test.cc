#include <filesystem>
#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "src/base/terminal_size.h"
#include "src/parent/overlay/overlay_footer.h"
#include "src/parent/test/support/test_paths.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"
#include "src/parent/worktree/overlay/worktree_management_overlay_test_support.h"

namespace {

using moe::parent::test_support::required_environment_path;
using moe::parent::test_support::save_empty_worktree_registry;
using moe::parent::test_support::worktree_overlay_mode_labels;

TEST(WorktreeManagementOverlayTest, CyclesThreeModesAndResetsRepositoryInput) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "overlay";
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};
  save_empty_worktree_registry(registry_path);

  std::unique_ptr<moe::parent::WorktreeManagementOverlay> overlay =
      moe::parent::WorktreeManagementOverlay::start("/unused/workspace_parent", registry_path, root,
                                                    "/unused/git", "/unused/fzf", {}, size);
  EXPECT_TRUE(overlay->redraw_output().starts_with(
      moe::parent::render_overlay_footer(worktree_overlay_mode_labels(), 0, size)));

  overlay->write_input("\t");
  EXPECT_TRUE(overlay->redraw_output().starts_with(
      moe::parent::render_overlay_footer(worktree_overlay_mode_labels(), 1, size)));
  EXPECT_NE(overlay->redraw_output().find("No registered repositories"), std::string::npos);
  EXPECT_EQ(overlay->opaque_region_start_row(), 16);

  overlay->write_input("\t/tmp/new-repository");
  std::string const repository_form = overlay->redraw_output();
  std::size_t const form_input_position = repository_form.find("> /tmp/new-repository");
  ASSERT_NE(form_input_position, std::string::npos);
  EXPECT_EQ(repository_form.find("[Add repository]"), std::string::npos);
  EXPECT_TRUE(repository_form.starts_with(
      moe::parent::render_overlay_footer(worktree_overlay_mode_labels(), 2, size)));
  EXPECT_EQ(overlay->opaque_region_start_row(), 16);

  overlay->write_input("\x1b[Z");
  EXPECT_TRUE(overlay->redraw_output().starts_with(
      moe::parent::render_overlay_footer(worktree_overlay_mode_labels(), 1, size)));

  overlay->write_input("\t");
  std::string const reset_repository_form = overlay->redraw_output();
  EXPECT_EQ(reset_repository_form.find("> /tmp/new-repository"), std::string::npos);
  EXPECT_NE(reset_repository_form.find("Repository root:"), std::string::npos);
}

}  // namespace
