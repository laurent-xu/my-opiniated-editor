#include "src/parent/runtime/parent_command_dispatcher_test_support.h"

#include <filesystem>
#include <string>

#include "src/parent/test/support/test_paths.h"

namespace moe::parent::test_support {

ParentCommandDispatcherConfig command_dispatcher_test_config(std::string_view const test_name) {
  std::filesystem::path const root =
      required_environment_path("TEST_TMPDIR") / std::string(test_name);
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return {
      .parent_executable = "/unused/workspace_parent",
      .worktree_registry_path = root / "state" / "worktrees.pb",
      .git_executable = runfile_path("test/fixtures/fake_git").string(),
      .fzf_executable = runfile_path("test/fixtures/fake_fzf").string(),
  };
}

}  // namespace moe::parent::test_support
