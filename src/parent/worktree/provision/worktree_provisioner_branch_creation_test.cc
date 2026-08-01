#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

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
using moe::parent::worktree_provisioner_test_support::read_file;
using moe::parent::worktree_provisioner_test_support::WorktreeProvisionerTest;

TEST_F(WorktreeProvisionerTest, DerivesPathByReplacingBranchSlashes) {
  EXPECT_EQ(moe::parent::derived_worktree_path(repository, "feature/terminal-status"),
            repository / "feature-terminal-status");
}

TEST_F(WorktreeProvisionerTest, CreatesBranchFromRepositoryDefaultAndRegistersWorktree) {
  std::filesystem::path const worktree = repository / "feature";
  std::ostringstream progress;

  moe::parent::WorktreeProvisionResult const result = provisioner.provision(
      WorktreeProvisionRequest{
          .repository_root = repository,
          .branch = "feature",
          .worktree_path = worktree,
          .registry_path = registry_path,
      },
      progress);

  EXPECT_EQ(result.kind, WorktreeProvisionKind::CREATED);
  EXPECT_EQ(result.worktree_path, worktree);
  EXPECT_TRUE(std::filesystem::exists(worktree / ".git"));
  WorktreeRegistry const registry = WorktreeRegistryStore(registry_path).load();
  ASSERT_EQ(registry.repositories(0).worktrees_size(), 1);
  EXPECT_EQ(registry.repositories(0).worktrees(0).path(), worktree.string());

  std::string const log = read_file(git_log);
  EXPECT_NE(log.find("\"fetch\", \"origin\", \"+refs/heads/feature:refs/remotes/origin/feature\""),
            std::string::npos);
  EXPECT_NE(log.find("\"symbolic-ref\", \"--quiet\", \"HEAD\""), std::string::npos);
  EXPECT_NE(log.find("\"worktree\", \"add\", \"-b\", \"feature\""), std::string::npos);
  EXPECT_NE(log.find("\"main\""), std::string::npos);
}

TEST_F(WorktreeProvisionerTest, ChecksOutExistingLocalBranchWhenRemoteFetchFails) {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_LOCAL_BRANCHES", "[\"master\"]", 1), 0);
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_FAIL_OPERATION", "fetch", 1), 0);
  std::filesystem::path const worktree = repository / "master";
  std::ostringstream progress;

  moe::parent::WorktreeProvisionResult const result = provisioner.provision(
      WorktreeProvisionRequest{
          .repository_root = repository,
          .branch = "master",
          .worktree_path = worktree,
          .registry_path = registry_path,
      },
      progress);

  EXPECT_EQ(result.kind, WorktreeProvisionKind::CREATED);
  std::string const log = read_file(git_log);
  EXPECT_NE(log.find("\"fetch\", \"origin\", \"+refs/heads/master:refs/remotes/origin/master\""),
            std::string::npos);
  EXPECT_NE(log.find("\"worktree\", \"add\", \"" + worktree.string() + "\", \"master\""),
            std::string::npos);
  EXPECT_EQ(log.find("\"worktree\", \"add\", \"-b\", \"master\""), std::string::npos);
}

TEST_F(WorktreeProvisionerTest, CreatesTrackingBranchWhenBranchExistsOnlyOnOrigin) {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_REMOTE_BRANCHES", "[\"release\"]", 1), 0);
  std::filesystem::path const worktree = repository / "release";
  std::ostringstream progress;

  moe::parent::WorktreeProvisionResult const result = provisioner.provision(
      WorktreeProvisionRequest{
          .repository_root = repository,
          .branch = "release",
          .worktree_path = worktree,
          .registry_path = registry_path,
      },
      progress);

  EXPECT_EQ(result.kind, WorktreeProvisionKind::CREATED);
  std::string const log = read_file(git_log);
  EXPECT_NE(log.find("\"worktree\", \"add\", \"--track\", \"-b\", \"release\""), std::string::npos);
  EXPECT_NE(log.find("\"origin/release\""), std::string::npos);
}

}  // namespace
