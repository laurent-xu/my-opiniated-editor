#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "gtest/gtest.h"
#include "src/parent/worktree/registration/repository_registration_request.h"
#include "src/parent/worktree/registration/worktree_repository_registrar_test_support.h"

namespace {

using moe::parent::RepositoryRegistrationRequest;
using moe::parent::worktree_repository_registrar_test_support::read_file;
using moe::parent::worktree_repository_registrar_test_support::WorktreeRepositoryRegistrarTest;

TEST_F(WorktreeRepositoryRegistrarTest, RejectsInvalidExistingRootBeforeGit) {
  std::filesystem::path const repository = root / "invalid-repository";
  std::filesystem::create_directories(repository);
  std::ofstream(repository / "unexpected") << "data";

  std::ostringstream progress;
  EXPECT_THROW(registrar.register_repository(
                   RepositoryRegistrationRequest{
                       .repository_root = repository,
                       .clone_url = std::nullopt,
                       .registry_path = registry_path,
                   },
                   progress),
               std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(git_log));
  EXPECT_FALSE(std::filesystem::exists(registry_path));
}

TEST_F(WorktreeRepositoryRegistrarTest, CloneFailureCleansNewRoot) {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_FAIL_OPERATION", "clone", 1), 0);
  std::filesystem::path const repository = root / "failed-clone";

  std::ostringstream progress;
  EXPECT_THROW(registrar.register_repository(
                   RepositoryRegistrationRequest{
                       .repository_root = repository,
                       .clone_url = "ssh://example.invalid/repository.git",
                       .registry_path = registry_path,
                   },
                   progress),
               std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(repository));
  EXPECT_FALSE(std::filesystem::exists(registry_path));
}

TEST_F(WorktreeRepositoryRegistrarTest, RejectsMissingDefaultBranch) {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_DEFAULT_BRANCH", "detached", 1), 0);
  std::filesystem::path const repository = root / "no-default-branch";

  std::ostringstream progress;
  EXPECT_THROW(registrar.register_repository(
                   RepositoryRegistrationRequest{
                       .repository_root = repository,
                       .clone_url = "ssh://example.invalid/repository.git",
                       .registry_path = registry_path,
                   },
                   progress),
               std::runtime_error);
  EXPECT_TRUE(std::filesystem::is_directory(repository / ".bare"));
  EXPECT_FALSE(std::filesystem::exists(registry_path));
}

TEST_F(WorktreeRepositoryRegistrarTest, FetchFailureLeavesRecoverableBareRootUnregistered) {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_FAIL_OPERATION", "fetch", 1), 0);
  std::filesystem::path const repository = root / "failed-fetch";

  std::ostringstream progress;
  EXPECT_THROW(registrar.register_repository(
                   RepositoryRegistrationRequest{
                       .repository_root = repository,
                       .clone_url = "ssh://example.invalid/repository.git",
                       .registry_path = registry_path,
                   },
                   progress),
               std::runtime_error);
  EXPECT_TRUE(std::filesystem::is_directory(repository / ".bare"));
  EXPECT_EQ(read_file(repository / ".git"), "gitdir: ./.bare\n");
  EXPECT_FALSE(std::filesystem::exists(registry_path));
}

}  // namespace
