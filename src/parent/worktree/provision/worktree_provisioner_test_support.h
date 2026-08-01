#pragma once

#include <filesystem>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/test/support/environment_guard.h"
#include "src/parent/test/support/test_paths.h"
#include "src/parent/worktree/provision/worktree_provisioner.h"

namespace moe::parent::worktree_provisioner_test_support {

std::filesystem::path test_directory(std::string const& name);

std::string read_file(std::filesystem::path const& path);

class WorktreeProvisionerTest : public testing::Test {
 protected:
  void SetUp() override;

  void create_existing_worktree(std::filesystem::path const& path, std::string const& branch);

  test_support::EnvironmentGuard log_guard{"MOE_FAKE_GIT_LOG"};
  test_support::EnvironmentGuard fail_guard{"MOE_FAKE_GIT_FAIL_OPERATION"};
  test_support::EnvironmentGuard branch_guard{"MOE_FAKE_GIT_DEFAULT_BRANCH"};
  test_support::EnvironmentGuard local_branches_guard{"MOE_FAKE_GIT_LOCAL_BRANCHES"};
  test_support::EnvironmentGuard remote_branches_guard{"MOE_FAKE_GIT_REMOTE_BRANCHES"};
  std::filesystem::path root =
      test_directory(testing::UnitTest::GetInstance()->current_test_info()->name());
  std::filesystem::path repository = root / "repository";
  std::filesystem::path registry_path = root / "state" / "worktrees.pb";
  std::filesystem::path git_log = root / "git.log";
  WorktreeProvisioner provisioner{test_support::runfile_path("test/fixtures/fake_git").string()};
};

}  // namespace moe::parent::worktree_provisioner_test_support
