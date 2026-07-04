#include "src/core/version.h"

#include <string_view>

#include "gtest/gtest.h"

namespace {

TEST(VersionTest, ExposesProjectName) {
  EXPECT_EQ(moe::core::ProjectName(), std::string_view("my-opiniated-editor"));
}

TEST(VersionTest, ExposesCurrentPhase) {
  EXPECT_EQ(moe::core::PhaseName(),
            std::string_view("phase-0-build-and-test-foundation"));
}

}  // namespace

