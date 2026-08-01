#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "src/parent/worktree/registration/repository_registration_request.h"
#include "src/parent/worktree/registration/worktree_repository_registrar_test_support.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace {

using moe::parent::RepositoryRegistrationRequest;
using moe::parent::WorktreeRegistryStore;
using moe::parent::persistence::WorktreeRegistry;
using moe::parent::worktree_repository_registrar_test_support::WorktreeRepositoryRegistrarTest;

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
