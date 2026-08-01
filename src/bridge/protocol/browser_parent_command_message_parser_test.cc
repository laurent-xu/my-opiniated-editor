#include <array>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "gtest/gtest.h"
#include "src/bridge/protocol/browser_application_message.h"
#include "src/bridge/protocol/browser_application_message_parser.h"
#include "src/bridge/protocol/browser_application_message_parser_test_support.h"

namespace {

using moe::bridge::protocol::BrowserApplicationMessage;
using moe::bridge::protocol::test::expect_parse_error;

template <typename Command, typename Verify>
void expect_parent_command(std::string_view const message, Verify verify) {
  std::optional<BrowserApplicationMessage> const parsed =
      moe::bridge::protocol::parse_browser_application_message(message);
  if (!parsed.has_value()) {
    FAIL() << "expected parent command for: " << message;
    return;
  }
  moe::parent::ParentInputCommand const* const parent_command =
      std::get_if<moe::parent::ParentInputCommand>(&parsed.value());
  ASSERT_NE(parent_command, nullptr) << message;
  Command const* const command = std::get_if<Command>(parent_command);
  ASSERT_NE(command, nullptr) << message;
  verify(*command);
}

TEST(BrowserApplicationMessageParserTest, ParsesOnlyAnonymousTrayNumbersOneThroughNine) {
  struct TestCase {
    std::string_view message;
    int expected_tray;
  };
  constexpr std::array<TestCase, 3> CASES{{
      {.message = R"(2{"tray":1})", .expected_tray = 1},
      {.message = R"(2{"tray": 9})", .expected_tray = 9},
      {.message = R"(2prefix "tray":2suffix)", .expected_tray = 2},
  }};

  for (TestCase const& test_case : CASES) {
    expect_parent_command<moe::parent::SwitchAnonymousTrayCommand>(
        test_case.message, [&test_case](moe::parent::SwitchAnonymousTrayCommand const& command) {
          EXPECT_EQ(command.tray_number.value(), test_case.expected_tray) << test_case.message;
        });
  }

  for (std::string_view const message : {
           "2",
           R"(2{"other":1})",
           R"(2{"tray":0})",
           R"(2{"tray":10})",
           R"(2{"tray":"2"})",
       }) {
    expect_parse_error(
        {.message = message, .expected_error = "tray switch payload requires tray 1 through 9"});
  }
}

TEST(BrowserApplicationMessageParserTest, ToggleMessagesContinueToIgnoreTheirPayloads) {
  for (std::string_view const message : {"3", "3ignored"}) {
    expect_parent_command<moe::parent::ToggleWorktreeOverlayCommand>(
        message, [](moe::parent::ToggleWorktreeOverlayCommand const&) {});
  }
  for (std::string_view const message : {"5", "5ignored"}) {
    expect_parent_command<moe::parent::ToggleCommandModeCommand>(
        message, [](moe::parent::ToggleCommandModeCommand const&) {});
  }
}

TEST(BrowserApplicationMessageParserTest, ParsesEveryWorktreePickerAction) {
  constexpr std::array<std::pair<std::string_view, moe::parent::TrayActionIntent>, 2> BEGIN_CASES{{
      {"6c", moe::parent::TrayActionIntent::CLEAR},
      {"6r", moe::parent::TrayActionIntent::REMOVE},
  }};
  for (auto const& [message, expected_action] : BEGIN_CASES) {
    expect_parent_command<moe::parent::BeginTrayActionCommand>(
        message, [message, expected_action](moe::parent::BeginTrayActionCommand const& command) {
          EXPECT_EQ(command.action, expected_action) << message;
        });
  }

  constexpr std::array<std::pair<std::string_view, moe::parent::ConfirmationDecision>, 2>
      RESOLVE_CASES{{
          {"6y", moe::parent::ConfirmationDecision::CONFIRM},
          {"6n", moe::parent::ConfirmationDecision::CANCEL},
      }};
  for (auto const& [message, expected_decision] : RESOLVE_CASES) {
    expect_parent_command<moe::parent::ResolveTrayActionCommand>(
        message,
        [message, expected_decision](moe::parent::ResolveTrayActionCommand const& command) {
          EXPECT_EQ(command.decision, expected_decision) << message;
        });
  }

  for (std::string_view const message : {"6", "6C", "6cc", "6x"}) {
    expect_parse_error(
        {.message = message, .expected_error = "worktree picker command requires c, r, y, or n"});
  }
}

TEST(BrowserApplicationMessageParserTest, ParsesEveryOverlayNavigationAction) {
  constexpr std::array<std::pair<std::string_view, moe::parent::OverlayNavigation>, 7> CASES{{
      {"7up", moe::parent::OverlayNavigation::UP},
      {"7down", moe::parent::OverlayNavigation::DOWN},
      {"7right", moe::parent::OverlayNavigation::RIGHT},
      {"7left", moe::parent::OverlayNavigation::LEFT},
      {"7tab", moe::parent::OverlayNavigation::TAB},
      {"7backtab", moe::parent::OverlayNavigation::BACKTAB},
      {"7enter", moe::parent::OverlayNavigation::ENTER},
  }};

  for (auto const& [message, expected_navigation] : CASES) {
    expect_parent_command<moe::parent::NavigateOverlayCommand>(
        message,
        [message, expected_navigation](moe::parent::NavigateOverlayCommand const& command) {
          EXPECT_EQ(command.navigation, expected_navigation) << message;
        });
  }

  for (std::string_view const message : {"7", "7UP", "7up ", "7unknown"}) {
    expect_parse_error(
        {.message = message, .expected_error = "worktree overlay navigation is invalid"});
  }
}

}  // namespace
