#include "src/parent/worktree_registry_store.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/test/support/environment_guard.h"

namespace {

using moe::parent::WorktreeRegistryStore;
using moe::parent::persistence::Repository;
using moe::parent::persistence::WorktreeRegistry;
using moe::parent::test_support::EnvironmentGuard;

std::filesystem::path required_test_tmpdir() {
  char const* value = std::getenv("TEST_TMPDIR");
  if (value == nullptr) {
    throw std::runtime_error("TEST_TMPDIR is required");
  }
  return {value};
}

std::filesystem::path test_registry_path(std::string const& name) {
  std::filesystem::path const directory = required_test_tmpdir() / name;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory / "worktrees.pb";
}

void write_bytes(std::filesystem::path const& path, std::string const& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("failed to open test file");
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed to write test file");
  }
}

std::string read_bytes(std::filesystem::path const& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open test file");
  }
  return {std::istreambuf_iterator<char>(input), {}};
}

Repository* add_repository(WorktreeRegistry& registry, std::filesystem::path const& root,
                           std::vector<std::filesystem::path> const& worktrees) {
  Repository* repository = registry.add_repositories();
  repository->set_root_path(root.string());
  for (std::filesystem::path const& worktree : worktrees) {
    repository->add_worktrees()->set_path(worktree.string());
  }
  return repository;
}

TEST(WorktreeRegistryStoreTest, MissingRegistryLoadsAsEmpty) {
  WorktreeRegistryStore const store(test_registry_path("missing"));

  WorktreeRegistry const registry = store.load();

  EXPECT_EQ(registry.format_version(), WorktreeRegistryStore::FORMAT_VERSION);
  EXPECT_TRUE(registry.repositories().empty());
}

TEST(WorktreeRegistryStoreTest, ResolvesXdgStatePathAndHomeFallback) {
  EnvironmentGuard const state_directory_guard("MOE_STATE_DIRECTORY");
  EnvironmentGuard const xdg_guard("XDG_STATE_HOME");
  EnvironmentGuard const home_guard("HOME");
  std::filesystem::path const test_root = required_test_tmpdir() / "state-path";
  std::filesystem::path const xdg_root = test_root / "xdg";
  std::filesystem::path const home_root = test_root / "home";

  ASSERT_EQ(::unsetenv("MOE_STATE_DIRECTORY"), 0);
  ASSERT_EQ(::setenv("XDG_STATE_HOME", xdg_root.c_str(), 1), 0);
  ASSERT_EQ(::setenv("HOME", home_root.c_str(), 1), 0);
  EXPECT_EQ(WorktreeRegistryStore::default_registry_path(),
            xdg_root / "my-opiniated-editor" / "worktrees.pb");

  ASSERT_EQ(::unsetenv("XDG_STATE_HOME"), 0);
  EXPECT_EQ(WorktreeRegistryStore::default_registry_path(),
            home_root / ".local" / "state" / "my-opiniated-editor" / "worktrees.pb");
}

TEST(WorktreeRegistryStoreTest, InstanceStateDirectoryOverridesUserStateRoots) {
  EnvironmentGuard const state_directory_guard("MOE_STATE_DIRECTORY");
  EnvironmentGuard const xdg_guard("XDG_STATE_HOME");
  EnvironmentGuard const home_guard("HOME");
  std::filesystem::path const test_root = required_test_tmpdir() / "instance-state-path";
  std::filesystem::path const instance_root = test_root / "manual-instance";

  ASSERT_EQ(::setenv("XDG_STATE_HOME", (test_root / "xdg").c_str(), 1), 0);
  ASSERT_EQ(::setenv("HOME", (test_root / "home").c_str(), 1), 0);
  ASSERT_EQ(::setenv("MOE_STATE_DIRECTORY", instance_root.c_str(), 1), 0);
  EXPECT_EQ(WorktreeRegistryStore::default_registry_path(), instance_root / "worktrees.pb");

  ASSERT_EQ(::setenv("MOE_STATE_DIRECTORY", "relative-state", 1), 0);
  EXPECT_THROW(static_cast<void>(WorktreeRegistryStore::default_registry_path()),
               std::runtime_error);
}

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
