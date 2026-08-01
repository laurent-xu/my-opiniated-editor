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
using moe::parent::test_support::test_registry_path;
using moe::parent::test_support::write_bytes;

TEST(WorktreeRegistryStoreTest, MissingRegistryLoadsAsEmpty) {
  WorktreeRegistryStore const store(test_registry_path("missing"));

  WorktreeRegistry const registry = store.load();

  EXPECT_EQ(registry.format_version(), WorktreeRegistryStore::FORMAT_VERSION);
  EXPECT_TRUE(registry.repositories().empty());
}

TEST(WorktreeRegistryStoreTest, RejectsUnsupportedFormatVersion) {
  std::filesystem::path const registry_path = test_registry_path("unsupported-version");
  WorktreeRegistry registry;
  registry.set_format_version(WorktreeRegistryStore::FORMAT_VERSION + 1);
  std::string bytes;
  ASSERT_TRUE(registry.SerializeToString(&bytes));
  write_bytes(registry_path, bytes);

  WorktreeRegistryStore const store(registry_path);
  EXPECT_THROW(static_cast<void>(store.load()), std::runtime_error);
}

TEST(WorktreeRegistryStoreTest, RejectsTruncatedRegistry) {
  std::filesystem::path const registry_path = test_registry_path("truncated");
  WorktreeRegistry registry = WorktreeRegistryStore::empty_registry();
  add_repository(registry, registry_path.parent_path() / "repository", {});
  std::string bytes;
  ASSERT_TRUE(registry.SerializeToString(&bytes));
  ASSERT_GT(bytes.size(), 1);
  bytes.resize(bytes.size() - 1);
  write_bytes(registry_path, bytes);

  WorktreeRegistryStore const store(registry_path);
  EXPECT_THROW(static_cast<void>(store.load()), std::runtime_error);
}

}  // namespace
