#include "src/parent/workspace_parent.h"

#include <string>

#include "gtest/gtest.h"

namespace {

TEST(WorkspaceParentTest, StartupBannerNamesProjectAndPhase) {
  std::string const banner = moe::parent::startup_banner();

  EXPECT_NE(banner.find("my-opiniated-editor"), std::string::npos);
  EXPECT_NE(banner.find("phase-0-build-and-test-foundation"), std::string::npos);
}

}  // namespace
