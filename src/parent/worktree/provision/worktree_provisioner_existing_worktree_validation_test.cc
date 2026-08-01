#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "gtest/gtest.h"
#include "src/parent/worktree/provision/worktree_provision_kind.h"
#include "src/parent/worktree/provision/worktree_provision_request.h"
#include "src/parent/worktree/provision/worktree_provision_result.h"
#include "src/parent/worktree/provision/worktree_provisioner_test_support.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace {

using moe::parent::WorktreeProvisionKind;
using moe::parent::WorktreeProvisionRequest;
using moe::parent::WorktreeRegistryStore;
using moe::parent::persistence::WorktreeRegistry;
using moe::parent::worktree_provisioner_test_support::WorktreeProvisionerTest;

TEST_F(WorktreeProvisionerTest, AdoptsMatchingExistingWorktreeWithoutRunningGit) {
  std::filesystem::path const worktree = repository / "existing";
  create_existing_worktree(worktree, "existing");
  std::ostringstream progress;

  moe::parent::WorktreeProvisionResult const result = provisioner.provision(
      WorktreeProvisionRequest{
          .repository_root = repository,
          .branch = "existing",
          .worktree_path = worktree,
          .registry_path = registry_path,
      },
      progress);

  EXPECT_EQ(result.kind, WorktreeProvisionKind::ADOPTED);
  EXPECT_FALSE(std::filesystem::exists(git_log));
  WorktreeRegistry const registry = WorktreeRegistryStore(registry_path).load();
  ASSERT_EQ(registry.repositories(0).worktrees_size(), 1);
  EXPECT_EQ(registry.repositories(0).worktrees(0).path(), worktree.string());
}

TEST_F(WorktreeProvisionerTest, RejectsExistingWorktreeForDifferentBranchWithoutGit) {
  std::filesystem::path const worktree = repository / "existing";
  create_existing_worktree(worktree, "other");
  std::ostringstream progress;

  EXPECT_THROW(static_cast<void>(provisioner.provision(
                   WorktreeProvisionRequest{
                       .repository_root = repository,
                       .branch = "expected",
                       .worktree_path = worktree,
                       .registry_path = registry_path,
                   },
                   progress)),
               std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(git_log));
}

TEST_F(WorktreeProvisionerTest, RejectsInvalidBranchBeforeRunningGit) {
  std::ostringstream progress;
  EXPECT_THROW(static_cast<void>(provisioner.provision(
                   WorktreeProvisionRequest{
                       .repository_root = repository,
                       .branch = "../escape",
                       .worktree_path = repository / "escape",
                       .registry_path = registry_path,
                   },
                   progress)),
               std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(git_log));
}

TEST_F(WorktreeProvisionerTest, RejectsWorktreePathOutsideRepositoryBeforeRunningGit) {
  std::ostringstream progress;
  EXPECT_THROW(static_cast<void>(provisioner.provision(
                   WorktreeProvisionRequest{
                       .repository_root = repository,
                       .branch = "outside",
                       .worktree_path = root / "outside",
                       .registry_path = registry_path,
                   },
                   progress)),
               std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(git_log));
}

}  // namespace
