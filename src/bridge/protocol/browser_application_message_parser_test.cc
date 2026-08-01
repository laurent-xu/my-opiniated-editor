#include "src/bridge/protocol/browser_application_message_parser.h"

#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

#include "gtest/gtest.h"
#include "src/bridge/protocol/browser_application_message.h"

namespace {

using moe::bridge::protocol::BrowserApplicationMessage;
using moe::bridge::protocol::BrowserTerminalInput;
using moe::bridge::protocol::BrowserTerminalResize;

struct ParseErrorExpectation {
  std::string_view message;
  std::string_view expected_error;
};

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

void expect_parse_error(ParseErrorExpectation const& expectation) {
  try {
    static_cast<void>(
        moe::bridge::protocol::parse_browser_application_message(expectation.message));
    FAIL() << "expected parse error for: " << expectation.message;
  } catch (std::runtime_error const& error) {
    EXPECT_EQ(error.what(), expectation.expected_error);
  }
}

TEST(BrowserApplicationMessageParserTest, IgnoresEmptyReservedAndUnknownMessages) {
  for (std::string_view const message : {"", "4reserved", "8unknown", "xunknown"}) {
    EXPECT_FALSE(moe::bridge::protocol::parse_browser_application_message(message).has_value());
  }
}

TEST(BrowserApplicationMessageParserTest, PreservesEmptyAndBinaryTerminalInput) {
  std::optional<BrowserApplicationMessage> const empty_input =
      moe::bridge::protocol::parse_browser_application_message("0");
  if (!empty_input.has_value()) {
    FAIL() << "empty terminal input was not parsed";
    return;
  }
  BrowserTerminalInput const* const empty = std::get_if<BrowserTerminalInput>(&empty_input.value());
  ASSERT_NE(empty, nullptr);
  EXPECT_TRUE(empty->bytes.empty());

  std::array<char, 4> const binary_message{'0', 'a', '\0', 'b'};
  std::optional<BrowserApplicationMessage> const binary_input =
      moe::bridge::protocol::parse_browser_application_message(
          std::string_view(binary_message.data(), binary_message.size()));
  if (!binary_input.has_value()) {
    FAIL() << "binary terminal input was not parsed";
    return;
  }
  BrowserTerminalInput const* const binary =
      std::get_if<BrowserTerminalInput>(&binary_input.value());
  ASSERT_NE(binary, nullptr);
  EXPECT_EQ(binary->bytes, std::string_view(binary_message.data() + 1, binary_message.size() - 1U));
}

TEST(BrowserApplicationMessageParserTest, ParsesExistingResizePayloadForms) {
  struct TestCase {
    std::string_view message;
    int expected_rows;
    int expected_columns;
  };
  constexpr std::array<TestCase, 4> CASES{{
      {.message = R"(1{"columns":80,"rows":24})", .expected_rows = 24, .expected_columns = 80},
      {.message = R"(1{"rows": 40, "columns": 120})", .expected_rows = 40, .expected_columns = 120},
      {.message = R"(1prefix "columns":90suffix "rows":30suffix)",
       .expected_rows = 30,
       .expected_columns = 90},
      {.message = R"(1{"columns":0,"rows":-1})", .expected_rows = -1, .expected_columns = 0},
  }};

  for (TestCase const& test_case : CASES) {
    std::optional<BrowserApplicationMessage> const parsed =
        moe::bridge::protocol::parse_browser_application_message(test_case.message);
    if (!parsed.has_value()) {
      FAIL() << "resize was not parsed: " << test_case.message;
      return;
    }
    BrowserTerminalResize const* const resize = std::get_if<BrowserTerminalResize>(&parsed.value());
    ASSERT_NE(resize, nullptr) << test_case.message;
    EXPECT_EQ(resize->size.rows, test_case.expected_rows) << test_case.message;
    EXPECT_EQ(resize->size.cols, test_case.expected_columns) << test_case.message;
  }
}

TEST(BrowserApplicationMessageParserTest, RejectsResizePayloadsMissingIntegerFields) {
  for (std::string_view const message : {
           "1",
           R"(1{"columns":80})",
           R"(1{"rows":24})",
           R"(1{"columns":"80","rows":24})",
           "1{\"columns\":\t80,\"rows\":24}",
       }) {
    expect_parse_error(
        {.message = message, .expected_error = "resize payload requires columns and rows"});
  }
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
