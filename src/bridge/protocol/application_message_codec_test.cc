#include "src/bridge/protocol/application_message_codec.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gtest/gtest.h"
#include "src/bridge/protocol/bridge_to_browser_message.h"
#include "src/bridge/protocol/browser_to_bridge_message.h"

namespace {

using moe::bridge::protocol::BridgeToBrowserMessage;
using moe::bridge::protocol::BrowserToBridgeMessage;

TEST(ApplicationMessageCodecTest, RejectsEmptyAndNulPrefixedBrowserMessages) {
  EXPECT_FALSE(moe::bridge::protocol::decode_browser_to_bridge_message("").has_value());

  std::array<char, 2> const nul_prefixed{'\0', '0'};
  EXPECT_FALSE(moe::bridge::protocol::decode_browser_to_bridge_message(
                   std::string_view(nul_prefixed.data(), nul_prefixed.size()))
                   .has_value());
}

TEST(ApplicationMessageCodecTest, PreservesEmptyAndNulContainingTerminalInputPayloads) {
  std::optional<BrowserToBridgeMessage> const empty_input =
      moe::bridge::protocol::decode_browser_to_bridge_message("0");
  if (!empty_input.has_value()) {
    FAIL() << "terminal input should decode";
    return;
  }
  EXPECT_EQ(empty_input->type, BrowserToBridgeMessage::Type::TERMINAL_INPUT);
  EXPECT_TRUE(empty_input->payload.empty());

  std::array<char, 4> const input_message{'0', 'a', '\0', 'b'};
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
  for (std::string_view const message : {"4reserved", "8unknown", "xunknown"}) {
    EXPECT_FALSE(moe::bridge::protocol::decode_browser_to_bridge_message(message).has_value());
  }
}

TEST(ApplicationMessageCodecTest, DecodesEveryExistingBrowserMessageType) {
  using Type = BrowserToBridgeMessage::Type;
  constexpr std::array<std::pair<char, Type>, 7> CASES{{
      {'0', Type::TERMINAL_INPUT},
      {'1', Type::RESIZE},
      {'2', Type::SWITCH_ANONYMOUS_TRAY},
      {'3', Type::TOGGLE_WORKTREE_OVERLAY},
      {'5', Type::TOGGLE_COMMAND_MODE},
      {'6', Type::WORKTREE_PICKER_ACTION},
      {'7', Type::OVERLAY_NAVIGATION},
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
  std::optional<BrowserToBridgeMessage> const terminal_input =
      moe::bridge::protocol::decode_browser_to_bridge_message("0input");
  std::optional<BrowserToBridgeMessage> const resize =
      moe::bridge::protocol::decode_browser_to_bridge_message("1resize");
  if (!terminal_input.has_value() || !resize.has_value()) {
    FAIL() << "direction-specific browser messages should decode";
    return;
  }
  EXPECT_EQ(terminal_input->type, BrowserToBridgeMessage::Type::TERMINAL_INPUT);
  EXPECT_EQ(resize->type, BrowserToBridgeMessage::Type::RESIZE);

  EXPECT_EQ(moe::bridge::protocol::encode_bridge_to_browser_message(
                {.type = BridgeToBrowserMessage::Type::TERMINAL_OUTPUT, .payload = "output"}),
            "0output");
  EXPECT_EQ(moe::bridge::protocol::encode_bridge_to_browser_message(
                {.type = BridgeToBrowserMessage::Type::PARENT_STATUS, .payload = "status"}),
            "1status");
}

TEST(ApplicationMessageCodecTest, PreservesBinaryServerPayloads) {
  std::array<char, 3> const payload{'a', '\0', 'b'};
  std::string const encoded = moe::bridge::protocol::encode_bridge_to_browser_message(
      {.type = BridgeToBrowserMessage::Type::TERMINAL_OUTPUT,
       .payload = std::string_view(payload.data(), payload.size())});

  ASSERT_EQ(encoded.size(), 4U);
  EXPECT_EQ(encoded.front(), '0');
  EXPECT_EQ(std::string_view(encoded).substr(1U), std::string_view(payload.data(), payload.size()));
}

}  // namespace
