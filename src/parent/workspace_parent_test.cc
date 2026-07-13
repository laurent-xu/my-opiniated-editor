#include "src/parent/workspace_parent.h"

#include <filesystem>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

TEST(WorkspaceParentTest, ConfiguredLoginShellIsAbsolutePath) {
  std::filesystem::path const shell = moe::parent::configured_login_shell();

  EXPECT_TRUE(shell.is_absolute()) << shell;
  EXPECT_FALSE(shell.empty());
}

TEST(WorkspaceParentTest, InteractiveShellCommandRunsShellInteractively) {
  std::vector<std::string> const command =
      moe::parent::interactive_shell_command(std::filesystem::path("/bin/example-shell"));

  ASSERT_EQ(command.size(), 2U);
  EXPECT_EQ(command[0], "/bin/example-shell");
  EXPECT_EQ(command[1], "-i");
}

}  // namespace
