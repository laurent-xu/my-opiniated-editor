#include "src/parent/shell/shell_configuration.h"

#include <filesystem>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

TEST(ShellConfigurationTest, ConfiguredHomeDirectoryIsAbsolutePath) {
  std::filesystem::path const home = moe::parent::configured_home_directory();

  EXPECT_TRUE(home.is_absolute()) << home;
  EXPECT_FALSE(home.empty());
}

TEST(ShellConfigurationTest, ConfiguredLoginShellIsAbsolutePath) {
  std::filesystem::path const shell = moe::parent::configured_login_shell();

  EXPECT_TRUE(shell.is_absolute()) << shell;
  EXPECT_FALSE(shell.empty());
}

TEST(ShellConfigurationTest, InteractiveShellCommandRunsShellInteractively) {
  std::vector<std::string> const command =
      moe::parent::interactive_shell_command(std::filesystem::path("/bin/example-shell"));

  ASSERT_EQ(command.size(), 2U);
  EXPECT_EQ(command[0], "/bin/example-shell");
  EXPECT_EQ(command[1], "-i");
}

TEST(ShellConfigurationTest, TerminalTypeDefaultsToXtermWhenMissing) {
  EXPECT_EQ(moe::parent::terminal_type_for_child(nullptr), "xterm-256color");
  EXPECT_EQ(moe::parent::terminal_type_for_child(""), "xterm-256color");
}

TEST(ShellConfigurationTest, TerminalTypeDefaultsToXtermWhenDumb) {
  EXPECT_EQ(moe::parent::terminal_type_for_child("dumb"), "xterm-256color");
}

TEST(ShellConfigurationTest, TerminalTypePreservesUsefulValue) {
  EXPECT_EQ(moe::parent::terminal_type_for_child("xterm-kitty"), "xterm-kitty");
}

}  // namespace
