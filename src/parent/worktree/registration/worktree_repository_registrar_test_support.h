#pragma once

#include <filesystem>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/test/support/environment_guard.h"
#include "src/parent/test/support/test_paths.h"
#include "src/parent/worktree/registration/worktree_repository_registrar.h"

namespace moe::parent::worktree_repository_registrar_test_support {

std::filesystem::path test_directory(std::string const& name);

void write_git_pointer(std::filesystem::path const& root);

std::string read_file(std::filesystem::path const& path);

class WorktreeRepositoryRegistrarTest : public testing::Test {
 protected:
  void SetUp() override;

  test_support::EnvironmentGuard log_guard{"MOE_FAKE_GIT_LOG"};
  test_support::EnvironmentGuard fail_guard{"MOE_FAKE_GIT_FAIL_OPERATION"};
  test_support::EnvironmentGuard branch_guard{"MOE_FAKE_GIT_DEFAULT_BRANCH"};
  test_support::EnvironmentGuard worktree_guard{"MOE_FAKE_GIT_WORKTREE_LIST"};
  std::filesystem::path root =
      test_directory(testing::UnitTest::GetInstance()->current_test_info()->name());
  std::filesystem::path registry_path = root / "state" / "worktrees.pb";
  std::filesystem::path git_log = root / "git.log";
  WorktreeRepositoryRegistrar registrar{
      test_support::runfile_path("test/fixtures/fake_git").string()};
};

}  // namespace moe::parent::worktree_repository_registrar_test_support
