#include "src/parent/worktree/provision/worktree_provisioner_test_support.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace moe::parent::worktree_provisioner_test_support {

std::filesystem::path test_directory(std::string const& name) {
  std::filesystem::path const path = test_support::required_environment_path("TEST_TMPDIR") / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::string read_file(std::filesystem::path const& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

void WorktreeProvisionerTest::SetUp() {
  std::filesystem::create_directories(repository / ".bare");
  std::ofstream(repository / ".git") << "gitdir: ./.bare\n";

  persistence::WorktreeRegistry registry = WorktreeRegistryStore::empty_registry();
  registry.add_repositories()->set_root_path(repository.string());
  WorktreeRegistryStore(registry_path).save(registry);

  ASSERT_EQ(::setenv("MOE_FAKE_GIT_LOG", git_log.c_str(), 1), 0);
  ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_FAIL_OPERATION"), 0);
  ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_DEFAULT_BRANCH"), 0);
  ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_LOCAL_BRANCHES"), 0);
  ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_REMOTE_BRANCHES"), 0);
}

void WorktreeProvisionerTest::create_existing_worktree(std::filesystem::path const& path,
                                                       std::string const& branch) {
  std::filesystem::path const administrative = repository / ".bare" / "worktrees" / path.filename();
  std::filesystem::create_directories(administrative);
  std::filesystem::create_directories(path);
  std::ofstream(path / ".git") << "gitdir: " << administrative.string() << '\n';
  std::ofstream(administrative / "HEAD") << "ref: refs/heads/" << branch << '\n';
}

}  // namespace moe::parent::worktree_provisioner_test_support
