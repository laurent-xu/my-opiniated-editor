#include <filesystem>
#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"
#include "src/parent/worktree/registry/worktree_registry_store_test_support.h"

namespace {

using moe::parent::WorktreeRegistryStore;
using moe::parent::persistence::WorktreeRegistry;
using moe::parent::test_support::add_repository;
using moe::parent::test_support::test_registry_path;

TEST(WorktreeRegistryStoreTest, RoundTripsNormalizedDeterministicRegistry) {
  std::filesystem::path const registry_path = test_registry_path("round-trip");
  std::filesystem::path const test_root = registry_path.parent_path();
  std::filesystem::path const repository_a = test_root / "repository-a";
  std::filesystem::path const repository_b = test_root / "repository-b";
  std::filesystem::create_directories(repository_a / "worktree-a");
  std::filesystem::create_directories(repository_b / "worktree-b");

  WorktreeRegistry registry = WorktreeRegistryStore::empty_registry();
  add_repository(registry, repository_b / ".",
                 {repository_b / "worktree-b", repository_b / "worktree-a"});
  add_repository(registry, repository_a, {repository_a / "worktree-a"});

  WorktreeRegistryStore const store(registry_path);
  store.save(registry);
  WorktreeRegistry const loaded = store.load();

  ASSERT_EQ(loaded.repositories_size(), 2);
  EXPECT_EQ(loaded.repositories(0).root_path(), repository_a.string());
  EXPECT_EQ(loaded.repositories(0).worktrees(0).path(), (repository_a / "worktree-a").string());
  EXPECT_EQ(loaded.repositories(1).root_path(), repository_b.string());
  ASSERT_EQ(loaded.repositories(1).worktrees_size(), 2);
  EXPECT_EQ(loaded.repositories(1).worktrees(0).path(), (repository_b / "worktree-a").string());
  EXPECT_EQ(loaded.repositories(1).worktrees(1).path(), (repository_b / "worktree-b").string());
}

TEST(WorktreeRegistryStoreTest, RejectsWorktreeTrackedByMultipleRepositories) {
  std::filesystem::path const registry_path = test_registry_path("duplicate-worktree");
  std::filesystem::path const shared_worktree = registry_path.parent_path() / "shared-worktree";
  WorktreeRegistry registry = WorktreeRegistryStore::empty_registry();
  add_repository(registry, registry_path.parent_path() / "repository-a", {shared_worktree});
  add_repository(registry, registry_path.parent_path() / "repository-b", {shared_worktree / "."});

  WorktreeRegistryStore const store(registry_path);
  EXPECT_THROW(store.save(registry), std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(registry_path));
}

TEST(WorktreeRegistryStoreTest, AtomicSaveLeavesNoTemporaryFile) {
  std::filesystem::path const registry_path = test_registry_path("temporary-file");
  WorktreeRegistryStore const store(registry_path);

  store.save(WorktreeRegistryStore::empty_registry());

  std::vector<std::filesystem::path> entries;
  for (std::filesystem::directory_entry const& entry :
       std::filesystem::directory_iterator(registry_path.parent_path())) {
    entries.push_back(entry.path().filename());
  }
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries.front(), registry_path.filename());
}

}  // namespace
