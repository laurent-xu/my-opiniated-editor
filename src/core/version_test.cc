#include "src/core/version.h"

#include <string_view>

#include "gtest/gtest.h"

namespace {

TEST(VersionTest, ExposesProjectName) {
  EXPECT_EQ(moe::core::project_name(), std::string_view("my-opiniated-editor"));
}

TEST(VersionTest, ExposesCurrentPhase) {
  EXPECT_EQ(moe::core::phase_name(), std::string_view("phase-1-parent-pty-spike"));
}

}  // namespace
