#include "src/parent/worktree_remover.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/test/support/environment_guard.h"
#include "src/parent/worktree_registry_store.h"

namespace {

using moe::parent::test_support::EnvironmentGuard;

std::filesystem::path required_environment_path(char const* name) {
  char const* const value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    throw std::runtime_error(std::string(name) + " is required");
  }
  return value;
}

std::filesystem::path runfile_path(std::filesystem::path const& path) {
  return required_environment_path("TEST_SRCDIR") / required_environment_path("TEST_WORKSPACE") /
         path;
}

struct TestState {
  std::filesystem::path root;
  std::filesystem::path repository;
  std::filesystem::path worktree;
  std::filesystem::path registry_path;
};

TestState create_state(std::string const& name, bool const create_worktree = true) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / name;
  std::filesystem::remove_all(root);
  std::filesystem::path const repository = root / "repository";
  std::filesystem::path const worktree = repository / "topic";
  std::filesystem::create_directories(repository / ".bare");
  if (create_worktree) {
    std::filesystem::create_directories(worktree);
    std::ofstream(worktree / ".git") << "gitdir: ../.bare/worktrees/topic\n";
  }

  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::parent::persistence::WorktreeRegistry registry =
      moe::parent::WorktreeRegistryStore::empty_registry();
  moe::parent::persistence::Repository* const entry = registry.add_repositories();
  entry->set_root_path(std::filesystem::weakly_canonical(repository).string());
  entry->add_worktrees()->set_path(std::filesystem::weakly_canonical(worktree).string());
  moe::parent::WorktreeRegistryStore(registry_path).save(registry);
  return {
      .root = root,
      .repository = std::filesystem::weakly_canonical(repository),
      .worktree = std::filesystem::weakly_canonical(worktree),
      .registry_path = registry_path,
  };
}

std::string porcelain(TestState const& state, bool const include_worktree) {
  std::string result = "worktree " + (state.repository / ".bare").string() + "\nHEAD 111\nbare\n\n";
  if (include_worktree) {
    result += "worktree " + state.worktree.string() + "\nHEAD 222\nbranch refs/heads/topic\n\n";
  }
  return result;
}

void configure_list(std::string const& value) {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_WORKTREE_LIST", value.c_str(), 1), 0);
}

moe::parent::WorktreeRemover remover() {
  return moe::parent::WorktreeRemover(runfile_path("test/fixtures/fake_git").string());
}

class WorktreeRemoverTest : public testing::Test {
 protected:
  WorktreeRemoverTest()
      : list_guard("MOE_FAKE_GIT_WORKTREE_LIST"),
        after_prune_guard("MOE_FAKE_GIT_WORKTREE_LIST_AFTER_PRUNE"),
        fail_guard("MOE_FAKE_GIT_FAIL_OPERATION"),
        prune_marker_guard("MOE_FAKE_GIT_PRUNE_MARKER"),
        log_guard("MOE_FAKE_GIT_LOG") {
    static_cast<void>(::unsetenv("MOE_FAKE_GIT_WORKTREE_LIST"));
    static_cast<void>(::unsetenv("MOE_FAKE_GIT_WORKTREE_LIST_AFTER_PRUNE"));
    static_cast<void>(::unsetenv("MOE_FAKE_GIT_FAIL_OPERATION"));
    static_cast<void>(::unsetenv("MOE_FAKE_GIT_PRUNE_MARKER"));
    static_cast<void>(::unsetenv("MOE_FAKE_GIT_LOG"));
  }

 private:
  EnvironmentGuard list_guard;
  EnvironmentGuard after_prune_guard;
  EnvironmentGuard fail_guard;
  EnvironmentGuard prune_marker_guard;
  EnvironmentGuard log_guard;
};

TEST_F(WorktreeRemoverTest, PurgesGitWorktreeAndUnregistersIt) {
  TestState const state = create_state("remove-live");
  configure_list(porcelain(state, true));

  remover().remove({.registry_path = state.registry_path, .worktree_path = state.worktree});

  EXPECT_FALSE(std::filesystem::exists(state.worktree));
  moe::parent::persistence::WorktreeRegistry const registry =
      moe::parent::WorktreeRegistryStore(state.registry_path).load();
  ASSERT_EQ(registry.repositories_size(), 1);
  EXPECT_EQ(registry.repositories(0).worktrees_size(), 0);
}

TEST_F(WorktreeRemoverTest, SilentlyUnregistersWorktreeAlreadyAbsentFromGit) {
  TestState const state = create_state("remove-already-purged");
  configure_list(porcelain(state, false));

  remover().remove({.registry_path = state.registry_path, .worktree_path = state.worktree});

  EXPECT_TRUE(std::filesystem::exists(state.worktree));
  EXPECT_EQ(moe::parent::WorktreeRegistryStore(state.registry_path)
                .load()
                .repositories(0)
                .worktrees_size(),
            0);
}

TEST_F(WorktreeRemoverTest, PrunesMissingWorktreeWhenRemoveCannotFindItsDirectory) {
  TestState const state = create_state("remove-missing", false);
  std::filesystem::path const marker = state.root / "pruned";
  configure_list(porcelain(state, true));
  std::string const after_prune = porcelain(state, false);
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_WORKTREE_LIST_AFTER_PRUNE", after_prune.c_str(), 1), 0);
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_FAIL_OPERATION", "worktree-remove", 1), 0);
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_PRUNE_MARKER", marker.c_str(), 1), 0);

  remover().remove({.registry_path = state.registry_path, .worktree_path = state.worktree});

  EXPECT_TRUE(std::filesystem::exists(marker));
  EXPECT_EQ(moe::parent::WorktreeRegistryStore(state.registry_path)
                .load()
                .repositories(0)
                .worktrees_size(),
            0);
}

TEST_F(WorktreeRemoverTest, PreservesRegistryWhenGitStillOwnsWorktreeAfterFailure) {
  TestState const state = create_state("remove-git-failure");
  configure_list(porcelain(state, true));
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_FAIL_OPERATION", "worktree-remove", 1), 0);

  EXPECT_THROW(
      remover().remove({.registry_path = state.registry_path, .worktree_path = state.worktree}),
      std::runtime_error);

  EXPECT_TRUE(std::filesystem::exists(state.worktree));
  EXPECT_EQ(moe::parent::WorktreeRegistryStore(state.registry_path)
                .load()
                .repositories(0)
                .worktrees_size(),
            1);
}

TEST_F(WorktreeRemoverTest, FailedAtomicSaveLeavesPreviousRegistryReadable) {
  TestState const state = create_state("remove-save-failure");
  configure_list(porcelain(state, true));
  std::filesystem::permissions(
      state.registry_path.parent_path(),
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);

  EXPECT_THROW(
      remover().remove({.registry_path = state.registry_path, .worktree_path = state.worktree}),
      std::runtime_error);

  std::filesystem::permissions(state.registry_path.parent_path(),
                               std::filesystem::perms::owner_all);
  EXPECT_EQ(moe::parent::WorktreeRegistryStore(state.registry_path)
                .load()
                .repositories(0)
                .worktrees_size(),
            1);
}

}  // namespace
