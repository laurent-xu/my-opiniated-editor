#include "src/process/command_runner.h"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"

namespace {

std::filesystem::path required_environment_path(char const* const name) {
  char const* const value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    throw std::runtime_error(std::string(name) + " is required");
  }
  return value;
}

std::filesystem::path fake_command_path() {
  return required_environment_path("TEST_SRCDIR") / required_environment_path("TEST_WORKSPACE") /
         "src/process/fake_command";
}

moe::process::CommandResult run_fake_command(
    std::string const& action, moe::process::StandardOutputMode const standard_output_mode) {
  return moe::process::run_command({fake_command_path().string(), action}, standard_output_mode);
}

TEST(CommandRunnerTest, ReportsSuccessfulProcess) {
  moe::process::CommandResult const result =
      run_fake_command("success", moe::process::StandardOutputMode::INHERIT);

  EXPECT_TRUE(result.exit_status.succeeded());
  EXPECT_EQ(result.exit_status.value(), 0);
  EXPECT_TRUE(result.standard_output.empty());
}

TEST(CommandRunnerTest, CapturesStandardOutput) {
  moe::process::CommandResult const result =
      run_fake_command("stdout", moe::process::StandardOutputMode::CAPTURE);

  EXPECT_TRUE(result.exit_status.succeeded());
  EXPECT_EQ(result.standard_output, "captured output\n");
}

TEST(CommandRunnerTest, ReportsNonzeroExitCode) {
  moe::process::CommandResult const result =
      run_fake_command("nonzero", moe::process::StandardOutputMode::INHERIT);

  EXPECT_FALSE(result.exit_status.succeeded());
  EXPECT_EQ(result.exit_status.value(), 23);
}

TEST(CommandRunnerTest, ReportsSignalAsShellCompatibleStatus) {
  moe::process::CommandResult const result =
      run_fake_command("signal", moe::process::StandardOutputMode::INHERIT);

  EXPECT_FALSE(result.exit_status.succeeded());
  EXPECT_EQ(result.exit_status.value(), 128 + SIGTERM);
}

}  // namespace
