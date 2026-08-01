#include "src/parent/worktree_management_overlay.h"

#include <poll.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/base/process_id.h"
#include "src/parent/overlay_footer.h"
#include "src/parent/worktree/worktree_registry_store.h"

namespace {

constexpr std::array<std::string_view, 3> MODE_LABELS{
    "Worktrees",
    "Add worktree",
    "Add repository",
};

TEST(OverlayFooterTest, HighlightsSelectionWithoutChangingLabelText) {
  constexpr std::array<std::string_view, 2> LABELS{"One", "Two"};

  EXPECT_EQ(moe::parent::render_overlay_footer(LABELS, 1, {.rows = 3, .cols = 12}),
            "\x1b[3;1H\x1b[48;5;236m\x1b[38;5;252mOne  \x1b[48;5;244mTwo"
            "\x1b[48;5;236m    \x1b[0m");
}

std::filesystem::path required_environment_path(char const* name) {
  char const* value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error(std::string(name) + " is required");
  }
  return {value};
}

std::filesystem::path runfile_path(std::filesystem::path const& path) {
  return required_environment_path("TEST_SRCDIR") / required_environment_path("TEST_WORKSPACE") /
         path;
}

TEST(WorktreeManagementOverlayTest, CyclesThreeModesAndResetsRepositoryInput) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "overlay";
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};

  moe::parent::persistence::WorktreeRegistry registry =
      moe::parent::WorktreeRegistryStore::empty_registry();
  moe::parent::WorktreeRegistryStore(registry_path).save(registry);

  std::unique_ptr<moe::parent::WorktreeManagementOverlay> overlay =
      moe::parent::WorktreeManagementOverlay::start("/unused/workspace_parent", registry_path, root,
                                                    "/unused/git", "/unused/fzf", {}, size);
  EXPECT_TRUE(overlay->redraw_output().starts_with(
      moe::parent::render_overlay_footer(MODE_LABELS, 0, size)));

  overlay->write_input("\t");
  EXPECT_TRUE(overlay->redraw_output().starts_with(
      moe::parent::render_overlay_footer(MODE_LABELS, 1, size)));
  EXPECT_NE(overlay->redraw_output().find("No registered repositories"), std::string::npos);

  overlay->write_input("\t/tmp/new-repository");
  std::string const repository_form = overlay->redraw_output();
  std::size_t const form_input_position = repository_form.find("> /tmp/new-repository");
  ASSERT_NE(form_input_position, std::string::npos);
  EXPECT_EQ(repository_form.find("[Add repository]"), std::string::npos);
  EXPECT_TRUE(
      repository_form.starts_with(moe::parent::render_overlay_footer(MODE_LABELS, 2, size)));

  overlay->write_input("\x1b[Z");
  EXPECT_TRUE(overlay->redraw_output().starts_with(
      moe::parent::render_overlay_footer(MODE_LABELS, 1, size)));

  overlay->write_input("\t");
  std::string const reset_repository_form = overlay->redraw_output();
  EXPECT_EQ(reset_repository_form.find("> /tmp/new-repository"), std::string::npos);
  EXPECT_NE(reset_repository_form.find("Repository root:"), std::string::npos);
}

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
  EXPECT_TRUE(redraw.starts_with(moe::parent::render_overlay_footer(MODE_LABELS, 1, size)));

  overlay->write_input("\t");
  EXPECT_TRUE(overlay->redraw_output().starts_with(
      moe::parent::render_overlay_footer(MODE_LABELS, 2, size)));
}

TEST(WorktreeManagementOverlayTest, WorktreePickerIncludesAndPreviewsUsedAnonymousTray) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "anonymous-picker";
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};
  moe::parent::WorktreeRegistryStore(registry_path)
      .save(moe::parent::WorktreeRegistryStore::empty_registry());
  std::vector<moe::parent::TraySnapshot> const session_trays{
      moe::parent::TraySnapshot{
          .id = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
          .label = "tray 1",
          .working_directory = root,
          .child_pid = moe::base::ProcessId{},
      },
  };

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
  EXPECT_GT(preview_request.size.rows, 0);

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

TEST(WorktreeManagementOverlayTest, ConfirmsRemovalOfHighlightedWorktreePickerTray) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / "remove-picker";
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::base::TerminalSize const size{.rows = 24, .cols = 100};
  moe::parent::WorktreeRegistryStore(registry_path)
      .save(moe::parent::WorktreeRegistryStore::empty_registry());
  std::vector<moe::parent::TraySnapshot> const session_trays{
      moe::parent::TraySnapshot{
          .id = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
          .label = "tray 1",
          .working_directory = root,
          .child_pid = moe::base::ProcessId{},
      },
  };
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
  moe::parent::WorktreeRegistryStore(registry_path)
      .save(moe::parent::WorktreeRegistryStore::empty_registry());
  std::vector<moe::parent::TraySnapshot> const session_trays{
      moe::parent::TraySnapshot{
          .id = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
          .label = "tray 1",
          .working_directory = root,
          .child_pid = moe::base::ProcessId{},
      },
  };
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
