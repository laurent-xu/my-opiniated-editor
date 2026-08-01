#include "src/process/process_exit_status.h"

#include <sys/wait.h>

#include <csignal>

#include "gtest/gtest.h"

namespace {

TEST(ProcessExitStatusTest, ConvertsExitedWaitStatus) {
  int const wait_status = 23 << 8;
  ASSERT_TRUE(WIFEXITED(wait_status));

  moe::process::ProcessExitStatus const exit_status =
      moe::process::ProcessExitStatus::from_wait_status(
          moe::process::ProcessWaitStatus(wait_status));

  EXPECT_EQ(exit_status.value(), 23);
}

TEST(ProcessExitStatusTest, ConvertsSignaledWaitStatus) {
  int const wait_status = SIGTERM;
  ASSERT_TRUE(WIFSIGNALED(wait_status));

  moe::process::ProcessExitStatus const exit_status =
      moe::process::ProcessExitStatus::from_wait_status(
          moe::process::ProcessWaitStatus(wait_status));

  EXPECT_EQ(exit_status.value(), 128 + SIGTERM);
}

TEST(ProcessExitStatusTest, UsesFailureFallbackForStoppedWaitStatus) {
  int const wait_status = (SIGSTOP << 8) | 0x7F;
  ASSERT_TRUE(WIFSTOPPED(wait_status));

  moe::process::ProcessExitStatus const exit_status =
      moe::process::ProcessExitStatus::from_wait_status(
          moe::process::ProcessWaitStatus(wait_status));

  EXPECT_EQ(exit_status.value(), 1);
}

}  // namespace
