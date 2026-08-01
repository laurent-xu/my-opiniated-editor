#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/worktree/registration/repository_registration_request.h"
#include "src/parent/worktree/registration/worktree_repository_registrar_test_support.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace {

using moe::parent::RepositoryRegistrationRequest;
using moe::parent::WorktreeRegistryStore;
using moe::parent::persistence::WorktreeRegistry;
using moe::parent::worktree_repository_registrar_test_support::read_file;
using moe::parent::worktree_repository_registrar_test_support::WorktreeRepositoryRegistrarTest;
using moe::parent::worktree_repository_registrar_test_support::write_git_pointer;

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

}  // namespace
