#include "src/parent/worktree_candidate_finder.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/worktree_registry_store.h"

namespace {

class EnvironmentGuard {
 public:
  explicit EnvironmentGuard(char const* variable_name)
      : name(variable_name),
        original(std::getenv(variable_name) == nullptr
                     ? std::nullopt
                     : std::optional<std::string>(std::getenv(variable_name))) {}

  EnvironmentGuard(EnvironmentGuard const&) = delete;
  EnvironmentGuard& operator=(EnvironmentGuard const&) = delete;

  ~EnvironmentGuard() {
    if (original.has_value()) {
      static_cast<void>(setenv(name.c_str(), original->c_str(), 1));
    } else {
      static_cast<void>(unsetenv(name.c_str()));
    }
  }

 private:
  std::string name;
  std::optional<std::string> original;
};

std::filesystem::path required_env_path(char const* const name) {
  char const* const value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    throw std::runtime_error("missing test environment variable: " + std::string(name));
  }
  return value;
}

std::filesystem::path runfile_path(std::filesystem::path const& path) {
  return required_env_path("TEST_SRCDIR") / required_env_path("TEST_WORKSPACE") / path;
}

TEST(WorktreeCandidateFinderTest, ReturnsOnlyLiveAvailableTrackedWorktrees) {
  EnvironmentGuard const list_guard("MOE_FAKE_GIT_WORKTREE_LIST");
  std::filesystem::path const root = required_env_path("TEST_TMPDIR") / "candidate-filter";
  std::filesystem::path const repository = root / "repository";
  std::filesystem::path const available = repository / "main";
  std::filesystem::path const prunable = repository / "stale";
  std::filesystem::path const missing = repository / "missing";
  std::filesystem::path const untracked = repository / "untracked";
  std::filesystem::create_directories(repository / ".bare");
  std::filesystem::create_directories(available / ".git");
  std::filesystem::create_directories(prunable / ".git");
  std::filesystem::create_directories(untracked / ".git");

  moe::parent::persistence::WorktreeRegistry registry =
      moe::parent::WorktreeRegistryStore::empty_registry();
  moe::parent::persistence::Repository* const entry = registry.add_repositories();
  entry->set_root_path(std::filesystem::weakly_canonical(repository).string());
  entry->add_worktrees()->set_path(std::filesystem::weakly_canonical(available).string());
  entry->add_worktrees()->set_path(std::filesystem::weakly_canonical(prunable).string());
  entry->add_worktrees()->set_path(std::filesystem::weakly_canonical(missing).string());
  std::filesystem::path const registry_path = root / "state" / "worktrees.pb";
  moe::parent::WorktreeRegistryStore(registry_path).save(registry);

  std::string const porcelain = "worktree " + (repository / ".bare").string() +
                                "\nHEAD 111\nbare\n\n"
                                "worktree " +
                                available.string() +
                                "\nHEAD 222\nbranch refs/heads/main\n\n"
                                "worktree " +
                                prunable.string() +
                                "\nHEAD 333\nprunable gitdir file points to missing\n\n"
                                "worktree " +
                                missing.string() +
                                "\nHEAD 444\nbranch refs/heads/missing\n\n"
                                "worktree " +
                                untracked.string() + "\nHEAD 555\nbranch refs/heads/untracked\n\n";
  ASSERT_EQ(setenv("MOE_FAKE_GIT_WORKTREE_LIST", porcelain.c_str(), 1), 0);

  moe::parent::WorktreeCandidateFinder const finder(
      runfile_path("test/fixtures/fake_git").string());
  std::vector<std::filesystem::path> const candidates = finder.find_available(registry_path);

  ASSERT_EQ(candidates.size(), 1U);
  EXPECT_EQ(candidates.front(), std::filesystem::weakly_canonical(available));
}

}  // namespace
