#include "src/bridge/protocol/application_message_codec.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace moe::bridge::protocol {
namespace {

constexpr char TERMINAL_INPUT_DISCRIMINATOR = '0';
constexpr char RESIZE_DISCRIMINATOR = '1';
constexpr char SWITCH_ANONYMOUS_TRAY_DISCRIMINATOR = '2';
constexpr char TOGGLE_WORKTREE_OVERLAY_DISCRIMINATOR = '3';
constexpr char TOGGLE_COMMAND_MODE_DISCRIMINATOR = '5';
constexpr char WORKTREE_PICKER_ACTION_DISCRIMINATOR = '6';
constexpr char OVERLAY_NAVIGATION_DISCRIMINATOR = '7';

constexpr char TERMINAL_OUTPUT_DISCRIMINATOR = '0';
constexpr char PARENT_STATUS_DISCRIMINATOR = '1';

char bridge_to_browser_discriminator(BridgeToBrowserMessage::Type const type) {
  switch (type) {
    case BridgeToBrowserMessage::Type::TERMINAL_OUTPUT:
      return TERMINAL_OUTPUT_DISCRIMINATOR;
    case BridgeToBrowserMessage::Type::PARENT_STATUS:
      return PARENT_STATUS_DISCRIMINATOR;
  }
  throw std::logic_error("invalid bridge-to-browser message type");
}

}  // namespace

std::optional<BrowserToBridgeMessage> decode_browser_to_bridge_message(
    std::string_view const message) {
  if (message.empty()) {
    return std::nullopt;
  }

  BrowserToBridgeMessage::Type type;
  switch (message.front()) {
    case TERMINAL_INPUT_DISCRIMINATOR:
      type = BrowserToBridgeMessage::Type::TERMINAL_INPUT;
      break;
    case RESIZE_DISCRIMINATOR:
      type = BrowserToBridgeMessage::Type::RESIZE;
      break;
    case SWITCH_ANONYMOUS_TRAY_DISCRIMINATOR:
      type = BrowserToBridgeMessage::Type::SWITCH_ANONYMOUS_TRAY;
      break;
    case TOGGLE_WORKTREE_OVERLAY_DISCRIMINATOR:
      type = BrowserToBridgeMessage::Type::TOGGLE_WORKTREE_OVERLAY;
      break;
    case TOGGLE_COMMAND_MODE_DISCRIMINATOR:
      type = BrowserToBridgeMessage::Type::TOGGLE_COMMAND_MODE;
      break;
    case WORKTREE_PICKER_ACTION_DISCRIMINATOR:
      type = BrowserToBridgeMessage::Type::WORKTREE_PICKER_ACTION;
      break;
    case OVERLAY_NAVIGATION_DISCRIMINATOR:
      type = BrowserToBridgeMessage::Type::OVERLAY_NAVIGATION;
      break;
    default:
      return std::nullopt;
  }
  return BrowserToBridgeMessage{.type = type, .payload = message.substr(1U)};
}

std::string encode_bridge_to_browser_message(BridgeToBrowserMessage const& message) {
  std::string encoded(1U, bridge_to_browser_discriminator(message.type));
  encoded.append(message.payload);
  return encoded;
}

}  // namespace moe::bridge::protocol
