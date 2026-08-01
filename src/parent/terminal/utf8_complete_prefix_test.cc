#include "src/parent/terminal/utf8_complete_prefix.h"

#include <string>
#include <string_view>

#include "gtest/gtest.h"

namespace {

using moe::parent::utf8_complete_prefix_size;

TEST(Utf8CompletePrefixTest, EmptyAndAsciiInputAreComplete) {
  EXPECT_EQ(utf8_complete_prefix_size(""), 0);
  EXPECT_EQ(utf8_complete_prefix_size(std::string_view("ascii\0text", 10)), 10);
}

TEST(Utf8CompletePrefixTest, CompleteValidSequencesAreComplete) {
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xC2\x80", 2)), 2);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xDF\xBF", 2)), 2);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xE0\xA0\x80", 3)), 3);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xED\x9F\xBF", 3)), 3);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xF0\x90\x80\x80", 4)), 4);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xF4\x8F\xBF\xBF", 4)), 4);
}

TEST(Utf8CompletePrefixTest, IncompleteValidSequenceAtStartHasNoCompletePrefix) {
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xC2", 1)), 0);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xE2\x94", 2)), 0);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xF0\x90\x80", 3)), 0);
}

TEST(Utf8CompletePrefixTest, OnlyIncompleteTrailingValidSequenceIsExcluded) {
  EXPECT_EQ(utf8_complete_prefix_size(std::string("ok\xC2", 3)), 2);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xC2\xA2\xE2\x94", 4)), 2);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("a\xF0\x90\x80", 4)), 1);
}

TEST(Utf8CompletePrefixTest, InvalidLeadBytesAreSingleCompleteBytes) {
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\x80\xBF\xC0\xC1\xF5\xFF", 6)), 6);
}

TEST(Utf8CompletePrefixTest, InvalidContinuationMakesAvailableBytesComplete) {
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xC2x", 2)), 2);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xE2\x94x", 3)), 3);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xF0\x90x", 3)), 3);
}

TEST(Utf8CompletePrefixTest, InvalidFirstContinuationBoundsRemainComplete) {
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xE0\x9F", 2)), 2);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xED\xA0", 2)), 2);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xF0\x8F", 2)), 2);
  EXPECT_EQ(utf8_complete_prefix_size(std::string("\xF4\x90", 2)), 2);
}

}  // namespace
