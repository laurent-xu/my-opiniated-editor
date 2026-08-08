#include "src/bridge/protocol/application_message_codec.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gtest/gtest.h"
#include "src/bridge/protocol/application_message_discriminators.h"
#include "src/bridge/protocol/bridge_to_browser_message.h"
#include "src/bridge/protocol/browser_to_bridge_message.h"

namespace {

using moe::bridge::protocol::BridgeToBrowserMessage;
using moe::bridge::protocol::BrowserToBridgeMessage;
namespace bridge_discriminator = moe::bridge::protocol::bridge_to_browser_discriminator;
namespace browser_discriminator = moe::bridge::protocol::browser_to_bridge_discriminator;

TEST(ApplicationMessageCodecTest, PreservesNamedWireDiscriminatorMapping) {
  EXPECT_EQ(browser_discriminator::TERMINAL_INPUT, '0');
  EXPECT_EQ(browser_discriminator::RESIZE, '1');
  EXPECT_EQ(browser_discriminator::SWITCH_ANONYMOUS_TRAY, '2');
  EXPECT_EQ(browser_discriminator::TOGGLE_WORKTREE_OVERLAY, '3');
  EXPECT_EQ(browser_discriminator::TOGGLE_COMMAND_MODE, '5');
  EXPECT_EQ(browser_discriminator::WORKTREE_PICKER_ACTION, '6');
  EXPECT_EQ(browser_discriminator::OVERLAY_NAVIGATION, '7');
  EXPECT_EQ(browser_discriminator::PANE_ACTION, '8');
  EXPECT_EQ(browser_discriminator::PANE_RESIZE, '9');

  EXPECT_EQ(bridge_discriminator::TERMINAL_OUTPUT, '0');
  EXPECT_EQ(bridge_discriminator::PARENT_STATUS, '1');
  EXPECT_EQ(bridge_discriminator::PANE_OUTPUT, '2');
}

TEST(ApplicationMessageCodecTest, RejectsEmptyAndNulPrefixedBrowserMessages) {
  EXPECT_FALSE(moe::bridge::protocol::decode_browser_to_bridge_message("").has_value());

  std::array<char, 2> const nul_prefixed{'\0', browser_discriminator::TERMINAL_INPUT};
  EXPECT_FALSE(moe::bridge::protocol::decode_browser_to_bridge_message(
                   std::string_view(nul_prefixed.data(), nul_prefixed.size()))
                   .has_value());
}

TEST(ApplicationMessageCodecTest, PreservesEmptyAndNulContainingTerminalInputPayloads) {
  std::string const empty_input_message(1U, browser_discriminator::TERMINAL_INPUT);
  std::optional<BrowserToBridgeMessage> const empty_input =
      moe::bridge::protocol::decode_browser_to_bridge_message(empty_input_message);
  if (!empty_input.has_value()) {
    FAIL() << "terminal input should decode";
    return;
  }
  EXPECT_EQ(empty_input->type, BrowserToBridgeMessage::Type::TERMINAL_INPUT);
  EXPECT_TRUE(empty_input->payload.empty());

  std::array<char, 4> const input_message{browser_discriminator::TERMINAL_INPUT, 'a', '\0', 'b'};
  std::optional<BrowserToBridgeMessage> const binary_input =
      moe::bridge::protocol::decode_browser_to_bridge_message(
          std::string_view(input_message.data(), input_message.size()));
  if (!binary_input.has_value()) {
    FAIL() << "binary terminal input should decode";
    return;
  }
  EXPECT_EQ(binary_input->type, BrowserToBridgeMessage::Type::TERMINAL_INPUT);
  EXPECT_EQ(binary_input->payload,
            std::string_view(input_message.data() + 1, input_message.size() - 1U));
}

TEST(ApplicationMessageCodecTest, RejectsReservedAndUnknownBrowserDiscriminators) {
  for (std::string_view const message : {"4reserved", "xunknown"}) {
    EXPECT_FALSE(moe::bridge::protocol::decode_browser_to_bridge_message(message).has_value());
  }
}

TEST(ApplicationMessageCodecTest, DecodesEveryExistingBrowserMessageType) {
  using Type = BrowserToBridgeMessage::Type;
  constexpr std::array<std::pair<char, Type>, 9> CASES{{
      {browser_discriminator::TERMINAL_INPUT, Type::TERMINAL_INPUT},
      {browser_discriminator::RESIZE, Type::RESIZE},
      {browser_discriminator::SWITCH_ANONYMOUS_TRAY, Type::SWITCH_ANONYMOUS_TRAY},
      {browser_discriminator::TOGGLE_WORKTREE_OVERLAY, Type::TOGGLE_WORKTREE_OVERLAY},
      {browser_discriminator::TOGGLE_COMMAND_MODE, Type::TOGGLE_COMMAND_MODE},
      {browser_discriminator::WORKTREE_PICKER_ACTION, Type::WORKTREE_PICKER_ACTION},
      {browser_discriminator::OVERLAY_NAVIGATION, Type::OVERLAY_NAVIGATION},
      {browser_discriminator::PANE_ACTION, Type::PANE_ACTION},
      {browser_discriminator::PANE_RESIZE, Type::PANE_RESIZE},
  }};

  for (auto const& [discriminator, expected_type] : CASES) {
    std::string message{discriminator, 'x'};
    std::optional<BrowserToBridgeMessage> const decoded =
        moe::bridge::protocol::decode_browser_to_bridge_message(message);
    if (!decoded.has_value()) {
      ADD_FAILURE() << "message discriminator should decode: " << discriminator;
      continue;
    }
    EXPECT_EQ(decoded->type, expected_type);
    EXPECT_EQ(decoded->payload, "x");
  }
}

TEST(ApplicationMessageCodecTest, ZeroAndOneHaveDirectionSpecificMeanings) {
  std::string terminal_input_message(1U, browser_discriminator::TERMINAL_INPUT);
  terminal_input_message.append("input");
  std::string resize_message(1U, browser_discriminator::RESIZE);
  resize_message.append("resize");
  std::optional<BrowserToBridgeMessage> const terminal_input =
      moe::bridge::protocol::decode_browser_to_bridge_message(terminal_input_message);
  std::optional<BrowserToBridgeMessage> const resize =
      moe::bridge::protocol::decode_browser_to_bridge_message(resize_message);
  if (!terminal_input.has_value() || !resize.has_value()) {
    FAIL() << "direction-specific browser messages should decode";
    return;
  }
  EXPECT_EQ(terminal_input->type, BrowserToBridgeMessage::Type::TERMINAL_INPUT);
  EXPECT_EQ(resize->type, BrowserToBridgeMessage::Type::RESIZE);

  std::string expected_terminal_output(1U, bridge_discriminator::TERMINAL_OUTPUT);
  expected_terminal_output.append("output");
  EXPECT_EQ(moe::bridge::protocol::encode_bridge_to_browser_message(
                {.type = BridgeToBrowserMessage::Type::TERMINAL_OUTPUT, .payload = "output"}),
            expected_terminal_output);

  std::string expected_parent_status(1U, bridge_discriminator::PARENT_STATUS);
  expected_parent_status.append("status");
  EXPECT_EQ(moe::bridge::protocol::encode_bridge_to_browser_message(
                {.type = BridgeToBrowserMessage::Type::PARENT_STATUS, .payload = "status"}),
            expected_parent_status);
}

TEST(ApplicationMessageCodecTest, PreservesBinaryServerPayloads) {
  std::array<char, 3> const payload{'a', '\0', 'b'};
  std::string const encoded = moe::bridge::protocol::encode_bridge_to_browser_message(
      {.type = BridgeToBrowserMessage::Type::TERMINAL_OUTPUT,
       .payload = std::string_view(payload.data(), payload.size())});

  ASSERT_EQ(encoded.size(), 4U);
  EXPECT_EQ(encoded.front(), bridge_discriminator::TERMINAL_OUTPUT);
  EXPECT_EQ(std::string_view(encoded).substr(1U), std::string_view(payload.data(), payload.size()));
}

}  // namespace
