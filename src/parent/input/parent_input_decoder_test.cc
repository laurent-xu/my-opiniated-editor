#include "src/parent/input/parent_input_decoder.h"

#include <array>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/input/command/parent_input_command.h"
#include "src/parent/input/event/parent_input_event.h"

namespace {

constexpr char PARENT_COMMAND_PREFIX = '\x18';

std::string command_bytes(char const command) { return {PARENT_COMMAND_PREFIX, command}; }

moe::parent::LiteralInputEvent const& literal_event(moe::parent::ParentInputEvent const& event) {
  return std::get<moe::parent::LiteralInputEvent>(event);
}

moe::parent::ParentInputCommand const& command_event(moe::parent::ParentInputEvent const& event) {
  return std::get<moe::parent::CommandInputEvent>(event).command;
}

TEST(ParentInputDecoderTest, ForwardsLiteralBytesWithoutACommandPrefix) {
  moe::parent::ParentInputDecoder decoder;

  std::vector<moe::parent::ParentInputEvent> const events = decoder.consume("literal input");

  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(literal_event(events[0]).bytes, "literal input");
}

TEST(ParentInputDecoderTest, EmitsEventsInOrderAroundARecognizedCommand) {
  moe::parent::ParentInputDecoder decoder;
  std::string bytes = "old surface";
  bytes += command_bytes('2');
  bytes += "new surface";

  std::vector<moe::parent::ParentInputEvent> const events = decoder.consume(bytes);

  ASSERT_EQ(events.size(), 3U);
  EXPECT_EQ(literal_event(events[0]).bytes, "old surface");
  auto const& switch_command =
      std::get<moe::parent::SwitchAnonymousTrayCommand>(command_event(events[1]));
  EXPECT_EQ(switch_command.tray_number.value(), 2);
  EXPECT_EQ(literal_event(events[2]).bytes, "new surface");
}

TEST(ParentInputDecoderTest, RetainsCommandPrefixAcrossConsumeCalls) {
  moe::parent::ParentInputDecoder decoder;
  std::string first_input = "before";
  first_input.push_back(PARENT_COMMAND_PREFIX);

  std::vector<moe::parent::ParentInputEvent> const first_events = decoder.consume(first_input);
  std::vector<moe::parent::ParentInputEvent> const second_events = decoder.consume("eafter");

  ASSERT_EQ(first_events.size(), 1U);
  EXPECT_EQ(literal_event(first_events[0]).bytes, "before");
  ASSERT_EQ(second_events.size(), 2U);
  EXPECT_TRUE(std::holds_alternative<moe::parent::ToggleCommandModeCommand>(
      command_event(second_events[0])));
  EXPECT_EQ(literal_event(second_events[1]).bytes, "after");
}

TEST(ParentInputDecoderTest, EmptyInputPreservesPendingCommandPrefix) {
  moe::parent::ParentInputDecoder decoder;
  std::string const prefix(1, PARENT_COMMAND_PREFIX);

  EXPECT_TRUE(decoder.consume(prefix).empty());
  EXPECT_TRUE(decoder.consume("").empty());
  std::vector<moe::parent::ParentInputEvent> const events = decoder.consume("w");

  ASSERT_EQ(events.size(), 1U);
  EXPECT_TRUE(
      std::holds_alternative<moe::parent::ToggleWorktreeOverlayCommand>(command_event(events[0])));
}

TEST(ParentInputDecoderTest, ReplaysUnknownCommandAsTwoLiteralByteEvents) {
  moe::parent::ParentInputDecoder decoder;

  std::vector<moe::parent::ParentInputEvent> const events = decoder.consume(command_bytes('q'));

  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(literal_event(events[0]).bytes, std::string(1, PARENT_COMMAND_PREFIX));
  EXPECT_EQ(literal_event(events[1]).bytes, "q");
}

TEST(ParentInputDecoderTest, TreatsASecondPrefixAsAnUnknownCommandByte) {
  moe::parent::ParentInputDecoder decoder;
  std::string bytes{PARENT_COMMAND_PREFIX, PARENT_COMMAND_PREFIX, 'e'};

  std::vector<moe::parent::ParentInputEvent> const events = decoder.consume(bytes);

  ASSERT_EQ(events.size(), 3U);
  EXPECT_EQ(literal_event(events[0]).bytes, std::string(1, PARENT_COMMAND_PREFIX));
  EXPECT_EQ(literal_event(events[1]).bytes, std::string(1, PARENT_COMMAND_PREFIX));
  EXPECT_EQ(literal_event(events[2]).bytes, "e");
}

TEST(ParentInputDecoderTest, DecodesAnonymousTrayCommandsOneThroughNine) {
  for (int tray_number = 1; tray_number <= 9; ++tray_number) {
    moe::parent::ParentInputDecoder decoder;
    std::vector<moe::parent::ParentInputEvent> const events =
        decoder.consume(command_bytes(static_cast<char>('0' + tray_number)));

    ASSERT_EQ(events.size(), 1U);
    auto const& command =
        std::get<moe::parent::SwitchAnonymousTrayCommand>(command_event(events[0]));
    EXPECT_EQ(command.tray_number.value(), tray_number);
  }
}

TEST(ParentInputDecoderTest, DecodesEveryOverlayNavigationIncludingEnter) {
  constexpr std::array<std::pair<char, moe::parent::OverlayNavigation>, 7> CASES{{
      {'A', moe::parent::OverlayNavigation::UP},
      {'B', moe::parent::OverlayNavigation::DOWN},
      {'C', moe::parent::OverlayNavigation::RIGHT},
      {'D', moe::parent::OverlayNavigation::LEFT},
      {'I', moe::parent::OverlayNavigation::TAB},
      {'Z', moe::parent::OverlayNavigation::BACKTAB},
      {'M', moe::parent::OverlayNavigation::ENTER},
  }};

  for (auto const& [command_byte, expected_navigation] : CASES) {
    moe::parent::ParentInputDecoder decoder;
    std::vector<moe::parent::ParentInputEvent> const events =
        decoder.consume(command_bytes(command_byte));

    ASSERT_EQ(events.size(), 1U);
    auto const& command = std::get<moe::parent::NavigateOverlayCommand>(command_event(events[0]));
    EXPECT_EQ(command.navigation, expected_navigation);
  }
}

TEST(ParentInputDecoderTest, DecodesToggleClearRemoveConfirmAndCancelCommands) {
  moe::parent::ParentInputDecoder decoder;
  std::string bytes;
  for (char const command : std::array{'e', 'w', 'c', 'r', 'y', 'n'}) {
    bytes += command_bytes(command);
  }

  std::vector<moe::parent::ParentInputEvent> const events = decoder.consume(bytes);

  ASSERT_EQ(events.size(), 6U);
  EXPECT_TRUE(
      std::holds_alternative<moe::parent::ToggleCommandModeCommand>(command_event(events[0])));
  EXPECT_TRUE(
      std::holds_alternative<moe::parent::ToggleWorktreeOverlayCommand>(command_event(events[1])));
  EXPECT_EQ(std::get<moe::parent::BeginTrayActionCommand>(command_event(events[2])).action,
            moe::parent::TrayActionIntent::CLEAR);
  EXPECT_EQ(std::get<moe::parent::BeginTrayActionCommand>(command_event(events[3])).action,
            moe::parent::TrayActionIntent::REMOVE);
  EXPECT_EQ(std::get<moe::parent::ResolveTrayActionCommand>(command_event(events[4])).decision,
            moe::parent::ConfirmationDecision::CONFIRM);
  EXPECT_EQ(std::get<moe::parent::ResolveTrayActionCommand>(command_event(events[5])).decision,
            moe::parent::ConfirmationDecision::CANCEL);
}

TEST(ParentInputDecoderTest, PreservesNulAndNonAsciiLiteralBytes) {
  moe::parent::ParentInputDecoder decoder;
  std::string const bytes{'a', '\0', static_cast<char>(0x80), static_cast<char>(0xFF), 'z'};

  std::vector<moe::parent::ParentInputEvent> const events = decoder.consume(bytes);

  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(literal_event(events[0]).bytes, bytes);
}

}  // namespace
