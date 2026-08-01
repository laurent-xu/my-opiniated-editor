#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/input/command/begin_tray_action_command.h"
#include "src/parent/input/command/confirmation_decision.h"
#include "src/parent/input/command/resolve_tray_action_command.h"
#include "src/parent/input/command/toggle_command_mode_command.h"
#include "src/parent/input/command/toggle_worktree_overlay_command.h"
#include "src/parent/input/command/tray_action_intent.h"
#include "src/parent/runtime/parent_command_dispatcher.h"
#include "src/parent/runtime/parent_command_dispatcher_config.h"
#include "src/parent/runtime/parent_command_dispatcher_test_support.h"
#include "src/parent/test/support/environment_guard.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/tray/tray_manager_test_support.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace {

using moe::parent::test_support::command_dispatcher_test_config;
using moe::parent::test_support::COMMAND_DISPATCHER_TEST_SIZE;

std::filesystem::path repository_path(moe::parent::ParentCommandDispatcherConfig const& config) {
  return config.worktree_registry_path.parent_path() / "repo";
}

std::filesystem::path worktree_path(moe::parent::ParentCommandDispatcherConfig const& config) {
  return repository_path(config) / "topic";
}

void save_tracked_worktree(moe::parent::ParentCommandDispatcherConfig const& config,
                           bool const create_bare_directory) {
  std::filesystem::path const repository = repository_path(config);
  std::filesystem::path const worktree = worktree_path(config);
  std::filesystem::create_directories(worktree / ".git");
  if (create_bare_directory) {
    std::filesystem::create_directories(repository / ".bare");
  }
  moe::parent::persistence::WorktreeRegistry registry =
      moe::parent::WorktreeRegistryStore::empty_registry();
  moe::parent::persistence::Repository* const tracked_repository = registry.add_repositories();
  tracked_repository->set_root_path(std::filesystem::weakly_canonical(repository).string());
  tracked_repository->add_worktrees()->set_path(
      std::filesystem::weakly_canonical(worktree).string());
  moe::parent::WorktreeRegistryStore(config.worktree_registry_path).save(registry);
}

void open_overlay_and_begin_removal(moe::parent::ParentCommandDispatcher& dispatcher) {
  static_cast<void>(dispatcher.dispatch(moe::parent::ToggleWorktreeOverlayCommand{},
                                        COMMAND_DISPATCHER_TEST_SIZE));
  static_cast<void>(
      dispatcher.dispatch(moe::parent::ToggleCommandModeCommand{}, COMMAND_DISPATCHER_TEST_SIZE));
  static_cast<void>(dispatcher.dispatch(
      moe::parent::BeginTrayActionCommand{.action = moe::parent::TrayActionIntent::REMOVE},
      COMMAND_DISPATCHER_TEST_SIZE));
}

moe::parent::ParentCommandDispatchEffects confirm_removal(
    moe::parent::ParentCommandDispatcher& dispatcher) {
  return dispatcher.dispatch(
      moe::parent::ResolveTrayActionCommand{
          .decision = moe::parent::ConfirmationDecision::CONFIRM,
      },
      COMMAND_DISPATCHER_TEST_SIZE);
}

TEST(ParentCommandDispatcherRemovalTest, BlocksProtectedWorktreeBeforeConfirmation) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcherConfig config =
      command_dispatcher_test_config("remove-protected-worktree");
  save_tracked_worktree(config, false);
  config.protected_worktree_path = std::filesystem::weakly_canonical(worktree_path(config));
  static_cast<void>(trays->switch_to_worktree(worktree_path(config)));
  moe::parent::TrayId const protected_tray = trays->active_id();
  moe::parent::ParentCommandDispatcher dispatcher(*trays, config);

  open_overlay_and_begin_removal(dispatcher);

  ASSERT_NE(trays->active_worktree_management_overlay(), nullptr);
  EXPECT_FALSE(trays->active_worktree_management_overlay()->has_tray_action_confirmation());
  EXPECT_NE(trays->active_worktree_management_overlay()->redraw_output().find(
                "Remove blocked: this worktree runs my-opiniated-editor"),
            std::string::npos);
  EXPECT_EQ(trays->active_id(), protected_tray);
  EXPECT_TRUE(std::filesystem::exists(worktree_path(config)));
  moe::parent::persistence::WorktreeRegistry const registry =
      moe::parent::WorktreeRegistryStore(config.worktree_registry_path).load();
  ASSERT_EQ(registry.repositories_size(), 1);
  EXPECT_EQ(registry.repositories(0).worktrees_size(), 1);
}

TEST(ParentCommandDispatcherRemovalTest, UnregistersWorktreeAlreadyAbsentFromGit) {
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcherConfig config =
      command_dispatcher_test_config("remove-unowned-worktree");
  save_tracked_worktree(config, false);
  static_cast<void>(trays->switch_to_worktree(worktree_path(config)));
  moe::parent::ParentCommandDispatcher dispatcher(*trays, config);
  open_overlay_and_begin_removal(dispatcher);

  moe::parent::ParentCommandDispatchEffects const effects = confirm_removal(dispatcher);

  EXPECT_EQ(trays->active_id(), moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()));
  moe::parent::persistence::WorktreeRegistry const registry =
      moe::parent::WorktreeRegistryStore(config.worktree_registry_path).load();
  ASSERT_EQ(registry.repositories_size(), 1);
  EXPECT_EQ(registry.repositories(0).worktrees_size(), 0);
  EXPECT_TRUE(effects.publish_status);
  EXPECT_TRUE(effects.redraw);
  EXPECT_TRUE(effects.trays_destroyed);
}

TEST(ParentCommandDispatcherRemovalTest, FailedGitRemovalPreservesRegistryAndTray) {
  moe::parent::test_support::EnvironmentGuard const list_guard("MOE_FAKE_GIT_WORKTREE_LIST");
  moe::parent::test_support::EnvironmentGuard const failure_guard("MOE_FAKE_GIT_FAIL_OPERATION");
  std::unique_ptr<moe::parent::TrayManager> trays = moe::parent::test_support::start_manager();
  moe::parent::ParentCommandDispatcherConfig config =
      command_dispatcher_test_config("failed-worktree-removal");
  save_tracked_worktree(config, true);
  std::filesystem::path const worktree = worktree_path(config);
  std::string const porcelain = "worktree " + (repository_path(config) / ".bare").string() +
                                "\nHEAD 111\nbare\n\nworktree " + worktree.string() +
                                "\nHEAD 222\nbranch refs/heads/topic\n\n";
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_WORKTREE_LIST", porcelain.c_str(), 1), 0);
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_FAIL_OPERATION", "worktree-remove", 1), 0);
  static_cast<void>(trays->switch_to_worktree(worktree));
  moe::parent::TrayId const worktree_tray = trays->active_id();
  moe::parent::ParentCommandDispatcher dispatcher(*trays, config);
  open_overlay_and_begin_removal(dispatcher);

  moe::parent::ParentCommandDispatchEffects const effects = confirm_removal(dispatcher);

  EXPECT_EQ(trays->active_id(), worktree_tray);
  moe::parent::persistence::WorktreeRegistry const registry =
      moe::parent::WorktreeRegistryStore(config.worktree_registry_path).load();
  ASSERT_EQ(registry.repositories_size(), 1);
  EXPECT_EQ(registry.repositories(0).worktrees_size(), 1);
  ASSERT_NE(trays->active_worktree_management_overlay(), nullptr);
  EXPECT_NE(trays->active_worktree_management_overlay()->redraw_output().find("Remove failed"),
            std::string::npos);
  EXPECT_TRUE(effects.publish_status);
  EXPECT_TRUE(effects.redraw);
  EXPECT_FALSE(effects.trays_destroyed);
}

}  // namespace
