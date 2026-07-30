#include "src/parent/worktree_provisioner.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "src/parent/worktree_registry_store.h"

namespace {

using moe::parent::WorktreeProvisioner;
using moe::parent::WorktreeProvisionKind;
using moe::parent::WorktreeProvisionRequest;
using moe::parent::WorktreeRegistryStore;
using moe::parent::persistence::WorktreeRegistry;

std::filesystem::path required_environment_path(char const* name) {
  char const* value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error(std::string(name) + " is required");
  }
  return {value};
}

std::filesystem::path runfile_path(std::filesystem::path const& path) {
  return required_environment_path("TEST_SRCDIR") / required_environment_path("TEST_WORKSPACE") /
         path;
}

std::filesystem::path test_directory(std::string const& name) {
  std::filesystem::path const path = required_environment_path("TEST_TMPDIR") / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::string read_file(std::filesystem::path const& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

class EnvironmentGuard {
 public:
  explicit EnvironmentGuard(std::string name)
      : name(std::move(name)), original(read_value(this->name)) {}

  EnvironmentGuard(EnvironmentGuard const&) = delete;
  EnvironmentGuard& operator=(EnvironmentGuard const&) = delete;

  ~EnvironmentGuard() {
    if (original.has_value()) {
      static_cast<void>(::setenv(name.c_str(), original->c_str(), 1));
    } else {
      static_cast<void>(::unsetenv(name.c_str()));
    }
  }

 private:
  static std::optional<std::string> read_value(std::string const& name) {
    char const* value = std::getenv(name.c_str());
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
  }

  std::string name;
  std::optional<std::string> original;
};

class WorktreeProvisionerTest : public testing::Test {
 protected:
  void SetUp() override {
    std::filesystem::create_directories(repository / ".bare");
    std::ofstream(repository / ".git") << "gitdir: ./.bare\n";

    WorktreeRegistry registry = WorktreeRegistryStore::empty_registry();
    registry.add_repositories()->set_root_path(repository.string());
    WorktreeRegistryStore(registry_path).save(registry);

    ASSERT_EQ(::setenv("MOE_FAKE_GIT_LOG", git_log.c_str(), 1), 0);
    ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_FAIL_OPERATION"), 0);
    ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_DEFAULT_BRANCH"), 0);
    ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_LOCAL_BRANCHES"), 0);
    ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_REMOTE_BRANCHES"), 0);
  }

  void create_existing_worktree(std::filesystem::path const& path, std::string const& branch) {
    std::filesystem::path const administrative =
        repository / ".bare" / "worktrees" / path.filename();
    std::filesystem::create_directories(administrative);
    std::filesystem::create_directories(path);
    std::ofstream(path / ".git") << "gitdir: " << administrative.string() << '\n';
    std::ofstream(administrative / "HEAD") << "ref: refs/heads/" << branch << '\n';
  }

  EnvironmentGuard log_guard{"MOE_FAKE_GIT_LOG"};
  EnvironmentGuard fail_guard{"MOE_FAKE_GIT_FAIL_OPERATION"};
  EnvironmentGuard branch_guard{"MOE_FAKE_GIT_DEFAULT_BRANCH"};
  EnvironmentGuard local_branches_guard{"MOE_FAKE_GIT_LOCAL_BRANCHES"};
  EnvironmentGuard remote_branches_guard{"MOE_FAKE_GIT_REMOTE_BRANCHES"};
  std::filesystem::path root =
      test_directory(testing::UnitTest::GetInstance()->current_test_info()->name());
  std::filesystem::path repository = root / "repository";
  std::filesystem::path registry_path = root / "state" / "worktrees.pb";
  std::filesystem::path git_log = root / "git.log";
  WorktreeProvisioner provisioner{runfile_path("test/fixtures/fake_git").string()};
};

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
