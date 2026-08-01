#include "src/parent/terminal/control_sequence/control_sequence_introducer_parser.h"

#include <array>
#include <string_view>

#include "gtest/gtest.h"

namespace moe::parent {
namespace {

TEST(ControlSequenceIntroducerParserTest, ClassifiesIncompleteAndNonIntroducerInputs) {
  EXPECT_EQ(parse_control_sequence_introducer_sequence("", 0).status,
            ControlSequenceIntroducerParseStatus::INCOMPLETE);
  EXPECT_EQ(parse_control_sequence_introducer_sequence("x", 0).status,
            ControlSequenceIntroducerParseStatus::INCOMPLETE);
  EXPECT_EQ(parse_control_sequence_introducer_sequence("xy", 0).status,
            ControlSequenceIntroducerParseStatus::NOT_CONTROL_SEQUENCE_INTRODUCER);
  EXPECT_EQ(parse_control_sequence_introducer_sequence("\x1b[12", 0).status,
            ControlSequenceIntroducerParseStatus::INCOMPLETE);
}

TEST(ControlSequenceIntroducerParserTest, ReportsSequenceBoundsAndFinalCommand) {
  std::string_view const bytes = "prefix\x1b[12Ksuffix";

  ControlSequenceIntroducerParseResult const result =
      parse_control_sequence_introducer_sequence(bytes, 6);

  ASSERT_EQ(result.status, ControlSequenceIntroducerParseStatus::COMPLETE);
  EXPECT_EQ(result.sequence.start, 6);
  EXPECT_EQ(result.sequence.end, 11);
  EXPECT_EQ(result.sequence.command, 'K');
  EXPECT_EQ(result.sequence.mode, 12);
}

TEST(ControlSequenceIntroducerParserTest, UsesZeroForAnEmptyFirstParameter) {
  ControlSequenceIntroducerParseResult const result =
      parse_control_sequence_introducer_sequence("\x1b[K", 0);

  ASSERT_EQ(result.status, ControlSequenceIntroducerParseStatus::COMPLETE);
  EXPECT_EQ(result.sequence.mode, 0);
}

TEST(ControlSequenceIntroducerParserTest, StopsAfterFirstSemicolonOrColonParameter) {
  EXPECT_EQ(parse_control_sequence_introducer_sequence("\x1b[12;34K", 0).sequence.mode, 12);
  EXPECT_EQ(parse_control_sequence_introducer_sequence("\x1b[7:8J", 0).sequence.mode, 7);
  EXPECT_EQ(parse_control_sequence_introducer_sequence("\x1b[;34K", 0).sequence.mode, 0);
}

TEST(ControlSequenceIntroducerParserTest, StopsAtFirstFinalByte) {
  ControlSequenceIntroducerParseResult const result =
      parse_control_sequence_introducer_sequence("\x1b[1K2J", 0);

  ASSERT_EQ(result.status, ControlSequenceIntroducerParseStatus::COMPLETE);
  EXPECT_EQ(result.sequence.end, 4);
  EXPECT_EQ(result.sequence.command, 'K');
  EXPECT_EQ(result.sequence.mode, 1);
}

TEST(ControlSequenceIntroducerParserTest, NullTerminatesFirstParameterInterpretation) {
  constexpr std::array<char, 6> BYTES{'\x1b', '[', '4', '\0', '2', 'K'};
  ControlSequenceIntroducerParseResult const result =
      parse_control_sequence_introducer_sequence(std::string_view(BYTES.data(), BYTES.size()), 0);

  ASSERT_EQ(result.status, ControlSequenceIntroducerParseStatus::COMPLETE);
  EXPECT_EQ(result.sequence.command, 'K');
  EXPECT_EQ(result.sequence.mode, 4);
}

TEST(ControlSequenceIntroducerParserTest, LeavesModeUnsetForUnsupportedParameterBytes) {
  ControlSequenceIntroducerParseResult const result =
      parse_control_sequence_introducer_sequence("\x1b[?25K", 0);

  ASSERT_EQ(result.status, ControlSequenceIntroducerParseStatus::COMPLETE);
  EXPECT_EQ(result.sequence.command, 'K');
  EXPECT_EQ(result.sequence.mode, -1);
}

TEST(ControlSequenceIntroducerParserTest, AcceptsFullFinalByteRange) {
  EXPECT_EQ(parse_control_sequence_introducer_sequence("\x1b[0@", 0).sequence.command, '@');
  EXPECT_EQ(parse_control_sequence_introducer_sequence("\x1b[0~", 0).sequence.command, '~');
  EXPECT_EQ(parse_control_sequence_introducer_sequence("\x1b[0?", 0).status,
            ControlSequenceIntroducerParseStatus::INCOMPLETE);
}

TEST(ControlSequenceIntroducerParserTest, ClassifiesOnlySupportedEraseCommandsWithParsedModes) {
  EXPECT_TRUE(is_erase_sequence(ControlSequenceIntroducerSequence{.command = 'K', .mode = 0}));
  EXPECT_TRUE(is_erase_sequence(ControlSequenceIntroducerSequence{.command = 'J', .mode = 3}));
  EXPECT_FALSE(is_erase_sequence(ControlSequenceIntroducerSequence{.command = 'K', .mode = -1}));
  EXPECT_FALSE(is_erase_sequence(ControlSequenceIntroducerSequence{.command = 'H', .mode = 0}));
}

}  // namespace
}  // namespace moe::parent
