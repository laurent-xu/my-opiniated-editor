#include "src/parent/worktree/registration/worktree_repository_registrar_test_support.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace moe::parent::worktree_repository_registrar_test_support {

std::filesystem::path test_directory(std::string const& name) {
  std::filesystem::path const path = test_support::required_environment_path("TEST_TMPDIR") / name;
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

void WorktreeRepositoryRegistrarTest::SetUp() {
  ASSERT_EQ(::setenv("MOE_FAKE_GIT_LOG", git_log.c_str(), 1), 0);
  ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_FAIL_OPERATION"), 0);
  ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_DEFAULT_BRANCH"), 0);
  ASSERT_EQ(::unsetenv("MOE_FAKE_GIT_WORKTREE_LIST"), 0);
}

}  // namespace moe::parent::worktree_repository_registrar_test_support
