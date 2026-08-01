#include <filesystem>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"
#include "src/parent/worktree/registry/worktree_registry_store_test_support.h"

namespace {

using moe::parent::WorktreeRegistryStore;
using moe::parent::persistence::WorktreeRegistry;
using moe::parent::test_support::add_repository;
using moe::parent::test_support::read_bytes;
using moe::parent::test_support::test_registry_path;
using moe::parent::test_support::write_bytes;

TEST(WorktreeRegistryStoreTest, CorruptRegistryIsNotOverwritten) {
  std::filesystem::path const registry_path = test_registry_path("corrupt");
  std::string const corrupt_bytes("\x0a\xff\xff", 3);
  write_bytes(registry_path, corrupt_bytes);

  WorktreeRegistryStore const store(registry_path);
  WorktreeRegistry const replacement = WorktreeRegistryStore::empty_registry();
  EXPECT_THROW(store.save(replacement), std::runtime_error);
  EXPECT_EQ(read_bytes(registry_path), corrupt_bytes);
}

TEST(WorktreeRegistryStoreTest, RejectedUpdateLeavesPreviousRegistryReadable) {
  std::filesystem::path const registry_path = test_registry_path("failed-update");
  std::filesystem::path const repository = registry_path.parent_path() / "repository";
  WorktreeRegistryStore const store(registry_path);

  WorktreeRegistry original = WorktreeRegistryStore::empty_registry();
  add_repository(original, repository, {});
  store.save(original);
  std::string const original_bytes = read_bytes(registry_path);

  WorktreeRegistry duplicate = original;
  add_repository(duplicate, repository / ".", {});
  EXPECT_THROW(store.save(duplicate), std::invalid_argument);

  EXPECT_EQ(read_bytes(registry_path), original_bytes);
  WorktreeRegistry const loaded = store.load();
  ASSERT_EQ(loaded.repositories_size(), 1);
  EXPECT_EQ(loaded.repositories(0).root_path(), repository.string());
}

}  // namespace
