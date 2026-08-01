#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include "gtest/gtest.h"
#include "src/parent/test/support/environment_guard.h"
#include "src/parent/test/support/test_paths.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace {

using moe::parent::WorktreeRegistryStore;
using moe::parent::test_support::EnvironmentGuard;
using moe::parent::test_support::required_environment_path;

TEST(WorktreeRegistryStoreTest, ResolvesXdgStatePathAndHomeFallback) {
  EnvironmentGuard const state_directory_guard("MOE_STATE_DIRECTORY");
  EnvironmentGuard const xdg_guard("XDG_STATE_HOME");
  EnvironmentGuard const home_guard("HOME");
  std::filesystem::path const test_root = required_environment_path("TEST_TMPDIR") / "state-path";
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
  std::filesystem::path const test_root =
      required_environment_path("TEST_TMPDIR") / "instance-state-path";
  std::filesystem::path const instance_root = test_root / "manual-instance";

  ASSERT_EQ(::setenv("XDG_STATE_HOME", (test_root / "xdg").c_str(), 1), 0);
  ASSERT_EQ(::setenv("HOME", (test_root / "home").c_str(), 1), 0);
  ASSERT_EQ(::setenv("MOE_STATE_DIRECTORY", instance_root.c_str(), 1), 0);
  EXPECT_EQ(WorktreeRegistryStore::default_registry_path(), instance_root / "worktrees.pb");

  ASSERT_EQ(::setenv("MOE_STATE_DIRECTORY", "relative-state", 1), 0);
  EXPECT_THROW(static_cast<void>(WorktreeRegistryStore::default_registry_path()),
               std::runtime_error);
}

}  // namespace
