#include "src/parent/workspace_parent.h"

#include <sstream>
#include <string>

#include "gtest/gtest.h"

namespace {

TEST(WorkspaceParentTest, StartupBannerNamesProjectAndPhase) {
  std::string const banner = moe::parent::startup_banner();

  EXPECT_NE(banner.find("my-opiniated-editor"), std::string::npos);
  EXPECT_NE(banner.find("phase-1-parent-pty-spike"), std::string::npos);
}

TEST(WorkspaceParentTest, StatusCommandReportsReadyParent) {
  std::string const response = moe::parent::handle_command(" status\r");

  EXPECT_NE(response.find("workspace-parent status=ready"), std::string::npos);
  EXPECT_NE(response.find("phase=phase-1-parent-pty-spike"), std::string::npos);
}

TEST(WorkspaceParentTest, InteractiveLoopPromptsRespondsAndExits) {
  std::istringstream input("status\nquit\n");
  std::ostringstream output;

  EXPECT_EQ(moe::parent::run_interactive(input, output), 0);

  std::string const transcript = output.str();
  EXPECT_NE(transcript.find("parent-ready"), std::string::npos);
  EXPECT_NE(transcript.find("moe> workspace-parent status=ready"), std::string::npos);
  EXPECT_NE(transcript.find("moe> goodbye"), std::string::npos);
}

}  // namespace
