#include "src/parent/worktree/overlay/worktree_overlay_process.h"

#include <poll.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/test/support/test_paths.h"

namespace {

using moe::parent::test_support::required_environment_path;
using moe::parent::test_support::runfile_path;

TEST(WorktreeOverlayProcessTest, SanitizesTranscriptAndRecordsFailedCompletion) {
  moe::parent::WorktreeOverlayProcess process;
  process.start(
      {runfile_path("src/parent/worktree/overlay/worktree_overlay_process_test_helper").string()},
      required_environment_path("TEST_TMPDIR"), {.rows = 7, .cols = 80});

  bool completed = false;
  for (int attempt = 0; attempt < 100 && !completed; ++attempt) {
    std::optional<moe::base::FileDescriptor> const descriptor = process.file_descriptor();
    if (!descriptor.has_value()) {
      FAIL() << "overlay process lost its PTY before reporting completion";
      return;
    }
    pollfd readable{.fd = descriptor->value(), .events = POLLIN, .revents = 0};
    if (::poll(&readable, 1, 50) == 1 && (readable.revents & POLLIN) != 0) {
      EXPECT_TRUE(process.read_process_output());
    }
    completed = process.refresh_process_state();
  }

  ASSERT_TRUE(completed);
  EXPECT_EQ(process.transcript_lines(),
            (std::vector<std::string>{"plain red", "caf\xC3!", "partial"}));
  EXPECT_TRUE(process.transcript_line().empty());
  EXPECT_FALSE(process.result_succeeded());
  EXPECT_FALSE(process.file_descriptor().has_value());
}

}  // namespace
