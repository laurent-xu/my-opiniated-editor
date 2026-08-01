#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/worktree/provision/worktree_provision_request.h"
#include "src/parent/worktree/provision/worktree_provision_result.h"
#include "src/parent/worktree/provision/worktree_provisioner_test_support.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace {

using moe::parent::WorktreeProvisionRequest;
using moe::parent::WorktreeRegistryStore;
using moe::parent::worktree_provisioner_test_support::WorktreeProvisionerTest;

TEST_F(WorktreeProvisionerTest, RejectsMissingDefaultBranchWithoutRegisteringWorktree) {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_DEFAULT_BRANCH", "detached", 1), 0);
  std::filesystem::path const worktree = repository / "missing-default";
  std::ostringstream progress;

  EXPECT_THROW(static_cast<void>(provisioner.provision(
                   WorktreeProvisionRequest{
                       .repository_root = repository,
                       .branch = "missing-default",
                       .worktree_path = worktree,
                       .registry_path = registry_path,
                   },
                   progress)),
               std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(worktree));
  EXPECT_TRUE(WorktreeRegistryStore(registry_path).load().repositories(0).worktrees().empty());
}

TEST_F(WorktreeProvisionerTest, GitFailureDoesNotRegisterWorktree) {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_FAIL_OPERATION", "worktree", 1), 0);
  std::filesystem::path const worktree = repository / "failed";
  std::ostringstream progress;

  EXPECT_THROW(static_cast<void>(provisioner.provision(
                   WorktreeProvisionRequest{
                       .repository_root = repository,
                       .branch = "failed",
                       .worktree_path = worktree,
                       .registry_path = registry_path,
                   },
                   progress)),
               std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(worktree));
  EXPECT_TRUE(WorktreeRegistryStore(registry_path).load().repositories(0).worktrees().empty());
}

TEST_F(WorktreeProvisionerTest, PersistenceFailureKeepsCreatedWorktree) {
  std::filesystem::path const worktree = repository / "partial";
  std::filesystem::permissions(registry_path.parent_path(), std::filesystem::perms::owner_read |
                                                                std::filesystem::perms::owner_exec);
  std::ostringstream progress;

  try {
    static_cast<void>(provisioner.provision(
        WorktreeProvisionRequest{
            .repository_root = repository,
            .branch = "partial",
            .worktree_path = worktree,
            .registry_path = registry_path,
        },
        progress));
    FAIL() << "expected registry update failure";
  } catch (std::runtime_error const& error) {
    EXPECT_NE(std::string(error.what()).find("worktree was created but registry update failed"),
              std::string::npos);
  }

  std::filesystem::permissions(registry_path.parent_path(), std::filesystem::perms::owner_all);
  EXPECT_TRUE(std::filesystem::exists(worktree / ".git"));
  EXPECT_TRUE(WorktreeRegistryStore(registry_path).load().repositories(0).worktrees().empty());
}

}  // namespace
