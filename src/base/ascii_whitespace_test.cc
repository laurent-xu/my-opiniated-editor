#include "src/base/ascii_whitespace.h"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>

namespace moe::base {
namespace {

static_assert(std::is_same_v<decltype(&trim_ascii_whitespace), std::string (*)(std::string)>);

TEST(AsciiWhitespaceTest, TrimsSpacesTabsCarriageReturnsAndNewlines) {
  EXPECT_EQ(trim_ascii_whitespace(" \t\r\nvalue\n\r\t "), "value");
  EXPECT_EQ(trim_ascii_whitespace("\n\r\t "), "");
}

TEST(AsciiWhitespaceTest, PreservesOtherAsciiWhitespaceCharacters) {
  EXPECT_EQ(trim_ascii_whitespace("\v\fvalue\f\v"), "\v\fvalue\f\v");
}

TEST(AsciiWhitespaceTest, PreservesInteriorWhitespaceAndTheInputValue) {
  std::string const input = " \tvalue \r\n with whitespace\n ";

  EXPECT_EQ(trim_ascii_whitespace(input), "value \r\n with whitespace");
  EXPECT_EQ(input, " \tvalue \r\n with whitespace\n ");
}

}  // namespace
}  // namespace moe::base
