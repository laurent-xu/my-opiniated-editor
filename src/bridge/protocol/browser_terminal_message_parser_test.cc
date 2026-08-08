#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include "gtest/gtest.h"
#include "src/bridge/protocol/browser_application_message.h"
#include "src/bridge/protocol/browser_application_message_parser.h"
#include "src/bridge/protocol/browser_application_message_parser_test_support.h"

namespace {

using moe::bridge::protocol::BrowserApplicationMessage;
using moe::bridge::protocol::BrowserTerminalInput;
using moe::bridge::protocol::BrowserTerminalResize;
using moe::bridge::protocol::test::expect_parse_error;

moe::parent::PaneId pane_id(std::uint64_t const value) {
  std::optional<moe::parent::PaneId> const result = moe::parent::PaneId::from_value(value);
  if (!result.has_value()) {
    throw std::logic_error("test pane id must be nonzero");
  }
  return *result;
}

TEST(BrowserApplicationMessageParserTest, IgnoresEmptyReservedAndUnknownMessages) {
  for (std::string_view const message : {"", "4reserved", "xunknown"}) {
    EXPECT_FALSE(moe::bridge::protocol::parse_browser_application_message(message).has_value());
  }
}

TEST(BrowserApplicationMessageParserTest, ParsesPaneViewportResize) {
  moe::parent::PaneViewOutput const identity{
      .tray_key = "anonymous:1",
      .pane_id = pane_id(3),
      .bytes = std::string(8, '\0'),
  };
  std::string payload = moe::parent::encode_pane_output_payload(identity);
  payload.resize(payload.size() - identity.bytes.size());
  payload.append("\0\0\0\x18\0\0\0\x50", 8);
  std::string message(1, '9');
  message.append(payload);

  std::optional<BrowserApplicationMessage> const parsed =
      moe::bridge::protocol::parse_browser_application_message(message);

  if (!parsed.has_value()) {
    FAIL() << "pane resize was not parsed";
    return;
  }
  moe::parent::PaneViewResize const* const resize =
      std::get_if<moe::parent::PaneViewResize>(&parsed.value());
  ASSERT_NE(resize, nullptr);
  EXPECT_EQ(resize->tray_key, "anonymous:1");
  EXPECT_EQ(resize->pane_id.value(), 3U);
  EXPECT_EQ(resize->size.rows, 24);
  EXPECT_EQ(resize->size.cols, 80);
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

}  // namespace
