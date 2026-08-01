#include "src/parent/input/parent_input_protocol.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "gtest/gtest.h"
#include "src/parent/input/command/parent_input_command.h"

namespace {

using moe::parent::BeginTrayActionCommand;
using moe::parent::ConfirmationDecision;
using moe::parent::NavigateOverlayCommand;
using moe::parent::OverlayNavigation;
using moe::parent::PaneCommand;
using moe::parent::PaneCommandAction;
using moe::parent::ParentInputCommand;
using moe::parent::ResolveTrayActionCommand;
using moe::parent::SwitchAnonymousTrayCommand;
using moe::parent::ToggleCommandModeCommand;
using moe::parent::ToggleWorktreeOverlayCommand;
using moe::parent::TrayActionIntent;
using moe::parent::TrayNumber;

constexpr std::uint8_t PARENT_COMMAND_PREFIX = 0x18U;

template <typename Value>
Value require_value(std::optional<Value> value) {
  if (!value.has_value()) {
    throw std::logic_error("expected decoded parent input command");
  }
  return std::move(*value);
}

std::array<std::pair<ParentInputCommand, char>, 13> fixed_command_cases() {
  return {{
      {ToggleCommandModeCommand{}, 'e'},
      {ToggleWorktreeOverlayCommand{}, 'w'},
      {BeginTrayActionCommand{.action = TrayActionIntent::CLEAR}, 'c'},
      {BeginTrayActionCommand{.action = TrayActionIntent::REMOVE}, 'r'},
      {ResolveTrayActionCommand{.decision = ConfirmationDecision::CONFIRM}, 'y'},
      {ResolveTrayActionCommand{.decision = ConfirmationDecision::CANCEL}, 'n'},
      {NavigateOverlayCommand{.navigation = OverlayNavigation::UP}, 'A'},
      {NavigateOverlayCommand{.navigation = OverlayNavigation::DOWN}, 'B'},
      {NavigateOverlayCommand{.navigation = OverlayNavigation::RIGHT}, 'C'},
      {NavigateOverlayCommand{.navigation = OverlayNavigation::LEFT}, 'D'},
      {NavigateOverlayCommand{.navigation = OverlayNavigation::TAB}, 'I'},
      {NavigateOverlayCommand{.navigation = OverlayNavigation::BACKTAB}, 'Z'},
      {NavigateOverlayCommand{.navigation = OverlayNavigation::ENTER}, 'M'},
  }};
}

TEST(ParentInputProtocolTest, RecognizesOnlyTheExistingCommandPrefix) {
  for (std::uint16_t byte = 0; byte <= 0xFFU; ++byte) {
    auto const candidate = static_cast<std::uint8_t>(byte);
    EXPECT_EQ(moe::parent::is_parent_input_command_prefix(candidate),
              candidate == PARENT_COMMAND_PREFIX);
  }
}

TEST(ParentInputProtocolTest, EncodesEveryFixedCommandWithItsExistingBytes) {
  for (auto const& [command, expected_command_byte] : fixed_command_cases()) {
    std::array<char, 2> const encoded = moe::parent::encode_parent_input_command(command);
    EXPECT_EQ(encoded, (std::array<char, 2>{static_cast<char>(PARENT_COMMAND_PREFIX),
                                            expected_command_byte}));
  }
}

TEST(ParentInputProtocolTest, EncodesAndDecodesAnonymousTraysOneThroughNine) {
  for (int tray = 1; tray <= 9; ++tray) {
    TrayNumber const tray_number = require_value(TrayNumber::from_int(tray));
    ParentInputCommand const command = SwitchAnonymousTrayCommand{.tray_number = tray_number};

    std::array<char, 2> const encoded = moe::parent::encode_parent_input_command(command);
    EXPECT_EQ(encoded, (std::array<char, 2>{static_cast<char>(PARENT_COMMAND_PREFIX),
                                            static_cast<char>('0' + tray)}));

    ParentInputCommand const decoded = require_value(
        moe::parent::decode_parent_input_command(static_cast<std::uint8_t>(encoded[1])));
    EXPECT_EQ(std::get<SwitchAnonymousTrayCommand>(decoded).tray_number.value(), tray);
  }
}

TEST(ParentInputProtocolTest, DecodesEveryFixedCommandToItsTypedMeaning) {
  ParentInputCommand const toggle_mode =
      require_value(moe::parent::decode_parent_input_command('e'));
  ParentInputCommand const toggle_worktree =
      require_value(moe::parent::decode_parent_input_command('w'));
  EXPECT_TRUE(std::holds_alternative<ToggleCommandModeCommand>(toggle_mode));
  EXPECT_TRUE(std::holds_alternative<ToggleWorktreeOverlayCommand>(toggle_worktree));

  ParentInputCommand const clear = require_value(moe::parent::decode_parent_input_command('c'));
  ParentInputCommand const remove = require_value(moe::parent::decode_parent_input_command('r'));
  EXPECT_EQ(std::get<BeginTrayActionCommand>(clear).action, TrayActionIntent::CLEAR);
  EXPECT_EQ(std::get<BeginTrayActionCommand>(remove).action, TrayActionIntent::REMOVE);

  ParentInputCommand const confirm = require_value(moe::parent::decode_parent_input_command('y'));
  ParentInputCommand const cancel = require_value(moe::parent::decode_parent_input_command('n'));
  EXPECT_EQ(std::get<ResolveTrayActionCommand>(confirm).decision, ConfirmationDecision::CONFIRM);
  EXPECT_EQ(std::get<ResolveTrayActionCommand>(cancel).decision, ConfirmationDecision::CANCEL);

  constexpr std::array<std::pair<char, OverlayNavigation>, 7> NAVIGATION_CASES{{
      {'A', OverlayNavigation::UP},
      {'B', OverlayNavigation::DOWN},
      {'C', OverlayNavigation::RIGHT},
      {'D', OverlayNavigation::LEFT},
      {'I', OverlayNavigation::TAB},
      {'Z', OverlayNavigation::BACKTAB},
      {'M', OverlayNavigation::ENTER},
  }};
  for (auto const& [command_byte, expected_navigation] : NAVIGATION_CASES) {
    ParentInputCommand const decoded = require_value(
        moe::parent::decode_parent_input_command(static_cast<std::uint8_t>(command_byte)));
    EXPECT_EQ(std::get<NavigateOverlayCommand>(decoded).navigation, expected_navigation);
  }
}

TEST(ParentInputProtocolTest, EncodesAndDecodesEveryPaneAction) {
  constexpr std::array<std::pair<PaneCommandAction, char>, 17> CASES{{
      {PaneCommandAction::UP, 'i'},
      {PaneCommandAction::DOWN, 'k'},
      {PaneCommandAction::LEFT, 'j'},
      {PaneCommandAction::RIGHT, 'l'},
      {PaneCommandAction::SPLIT_LEFT_TO_RIGHT, 'v'},
      {PaneCommandAction::SPLIT_ABOVE_BELOW, 'h'},
      {PaneCommandAction::TOGGLE_SELECTION_OR_SWAP, 's'},
      {PaneCommandAction::PROMOTE, '['},
      {PaneCommandAction::DESCEND, ']'},
      {PaneCommandAction::GROW, '+'},
      {PaneCommandAction::SHRINK, '-'},
      {PaneCommandAction::EQUALIZE, '='},
      {PaneCommandAction::TOGGLE_MOVE, 'm'},
      {PaneCommandAction::CONFIRM_MOVE, '\r'},
      {PaneCommandAction::ROTATE, 't'},
      {PaneCommandAction::TOGGLE_MAXIMIZE, 'z'},
      {PaneCommandAction::CLOSE, 'x'},
  }};

  for (auto const& [action, command_byte] : CASES) {
    std::array<char, 2> const encoded =
        moe::parent::encode_parent_input_command(PaneCommand{.action = action});
    EXPECT_EQ(encoded,
              (std::array<char, 2>{static_cast<char>(PARENT_COMMAND_PREFIX), command_byte}));

    ParentInputCommand const decoded = require_value(
        moe::parent::decode_parent_input_command(static_cast<std::uint8_t>(command_byte)));
    EXPECT_EQ(std::get<PaneCommand>(decoded).action, action);
  }
}

TEST(ParentInputProtocolTest, RejectsBytesWithoutAParentCommandMeaning) {
  for (std::uint8_t const byte : std::array<std::uint8_t, 6>{0x00U, 0x18U, '0', 'f', 'q', 0xFFU}) {
    EXPECT_FALSE(moe::parent::decode_parent_input_command(byte).has_value());
  }
}

}  // namespace
