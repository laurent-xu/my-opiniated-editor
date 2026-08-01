#include "src/parent/worktree/worktree_repository_registrar.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/test/support/environment_guard.h"
#include "src/parent/test/support/test_paths.h"
#include "src/parent/worktree/repository_registration_request.h"
#include "src/parent/worktree/worktree_registry_store.h"

namespace {

using moe::parent::RepositoryRegistrationRequest;
using moe::parent::WorktreeRegistryStore;
using moe::parent::WorktreeRepositoryRegistrar;
using moe::parent::persistence::WorktreeRegistry;
using moe::parent::test_support::EnvironmentGuard;
using moe::parent::test_support::required_environment_path;
using moe::parent::test_support::runfile_path;

std::filesystem::path test_directory(std::string const& name) {
  std::filesystem::path const path = required_environment_path("TEST_TMPDIR") / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

void write_git_pointer(std::filesystem::path const& root) {
  std::ofstream output(root / ".git");
  output << "gitdir: ./.bare\n";
}

std::string read_file(std::filesystem::path const& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

class WorktreeRepositoryRegistrarTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(::setenv("MOE_FAKE_GIT_LOG", git_log.c_str(), 1), 0);
    ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_FAIL_OPERATION"), 0);
    ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_DEFAULT_BRANCH"), 0);
    ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_WORKTREE_LIST"), 0);
  }

  EnvironmentGuard log_guard{"MOE_FAKE_GIT_LOG"};
  EnvironmentGuard fail_guard{"MOE_FAKE_GIT_FAIL_OPERATION"};
  EnvironmentGuard branch_guard{"MOE_FAKE_GIT_DEFAULT_BRANCH"};
  EnvironmentGuard worktree_guard{"MOE_FAKE_GIT_WORKTREE_LIST"};
  std::filesystem::path root =
      test_directory(testing::UnitTest::GetInstance()->current_test_info()->name());
  std::filesystem::path registry_path = root / "state" / "worktrees.pb";
  std::filesystem::path git_log = root / "git.log";
  WorktreeRepositoryRegistrar registrar{runfile_path("test/fixtures/fake_git").string()};
};

TEST_F(WorktreeRepositoryRegistrarTest, RegistersExistingBareRootAndAvailableWorktrees) {
  std::filesystem::path const repository = root / "repository";
  std::filesystem::path const available_worktree = repository / "main";
  std::filesystem::path const prunable_worktree = repository / "stale";
  std::filesystem::create_directories(repository / ".bare");
  std::filesystem::create_directories(available_worktree);
  write_git_pointer(repository);

  std::string const porcelain =
      "worktree " + (repository / ".bare").string() + "\nHEAD 111\nbare\n\nworktree " +
      available_worktree.string() + "\nHEAD 222\nbranch refs/heads/main\n\nworktree " +
      prunable_worktree.string() + "\nHEAD 333\nprunable gitdir file points to missing\n\n";
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_WORKTREE_LIST", porcelain.c_str(), 1), 0);

  std::ostringstream progress;
  registrar.register_repository(
      RepositoryRegistrationRequest{
          .repository_root = repository,
          .clone_url = std::nullopt,
          .registry_path = registry_path,
      },
      progress);

  WorktreeRegistry const registry = WorktreeRegistryStore(registry_path).load();
  ASSERT_EQ(registry.repositories_size(), 1);
  EXPECT_EQ(registry.repositories(0).root_path(),
            std::filesystem::weakly_canonical(repository).string());
  ASSERT_EQ(registry.repositories(0).worktrees_size(), 1);
  EXPECT_EQ(registry.repositories(0).worktrees(0).path(),
            std::filesystem::weakly_canonical(available_worktree).string());
  EXPECT_NE(progress.str().find("Repository registered"), std::string::npos);
}

TEST_F(WorktreeRepositoryRegistrarTest, CreatesBareRootWithoutInitialWorktree) {
  std::filesystem::path const repository = root / "new-repository";
  std::ostringstream progress;

  registrar.register_repository(
      RepositoryRegistrationRequest{
          .repository_root = repository,
          .clone_url = "ssh://example.invalid/repository.git",
          .registry_path = registry_path,
      },
      progress);

  EXPECT_TRUE(std::filesystem::is_directory(repository / ".bare"));
  EXPECT_EQ(read_file(repository / ".git"), "gitdir: ./.bare\n");
  WorktreeRegistry const registry = WorktreeRegistryStore(registry_path).load();
  ASSERT_EQ(registry.repositories_size(), 1);
  EXPECT_EQ(registry.repositories(0).root_path(),
            std::filesystem::weakly_canonical(repository).string());
  EXPECT_TRUE(registry.repositories(0).worktrees().empty());

  std::string const invocations = read_file(git_log);
  EXPECT_NE(invocations.find("\"clone\", \"--bare\""), std::string::npos);
  EXPECT_NE(invocations.find("\"config\", \"remote.origin.fetch\""), std::string::npos);
  EXPECT_NE(invocations.find("\"fetch\", \"origin\""), std::string::npos);
}

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

TEST_F(WorktreeRepositoryRegistrarTest, AdoptsCreatedRepositoryAfterRegistryConflictIsResolved) {
  std::filesystem::path const existing_repository = root / "existing";
  std::filesystem::path const shared_worktree = root / "shared-worktree";
  std::filesystem::create_directories(existing_repository);
  std::filesystem::create_directories(shared_worktree);

  WorktreeRegistry registry = WorktreeRegistryStore::empty_registry();
  auto* existing = registry.add_repositories();
  existing->set_root_path(existing_repository.string());
  existing->add_worktrees()->set_path(shared_worktree.string());
  WorktreeRegistryStore(registry_path).save(registry);

  std::filesystem::path const repository = root / "created";
  std::string const conflicting_porcelain =
      "worktree " + shared_worktree.string() + "\nHEAD 111\nbranch refs/heads/main\n\n";
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_WORKTREE_LIST", conflicting_porcelain.c_str(), 1), 0);

  std::ostringstream first_progress;
  EXPECT_THROW(registrar.register_repository(
                   RepositoryRegistrationRequest{
                       .repository_root = repository,
                       .clone_url = "ssh://example.invalid/repository.git",
                       .registry_path = registry_path,
                   },
                   first_progress),
               std::runtime_error);
  EXPECT_TRUE(std::filesystem::is_directory(repository / ".bare"));
  EXPECT_EQ(WorktreeRegistryStore(registry_path).load().repositories_size(), 1);

  ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_WORKTREE_LIST"), 0);
  std::ostringstream retry_progress;
  registrar.register_repository(
      RepositoryRegistrationRequest{
          .repository_root = repository,
          .clone_url = std::nullopt,
          .registry_path = registry_path,
      },
      retry_progress);

  WorktreeRegistry const recovered = WorktreeRegistryStore(registry_path).load();
  ASSERT_EQ(recovered.repositories_size(), 2);
  EXPECT_NE(retry_progress.str().find("Repository registered"), std::string::npos);
}

}  // namespace
