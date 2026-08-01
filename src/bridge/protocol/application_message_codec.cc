#include "src/bridge/protocol/application_message_codec.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "src/bridge/protocol/application_message_discriminators.h"

namespace moe::bridge::protocol {
namespace {

char discriminator_for_bridge_message(BridgeToBrowserMessage::Type const type) {
  switch (type) {
    case BridgeToBrowserMessage::Type::TERMINAL_OUTPUT:
      return bridge_to_browser_discriminator::TERMINAL_OUTPUT;
    case BridgeToBrowserMessage::Type::PARENT_STATUS:
      return bridge_to_browser_discriminator::PARENT_STATUS;
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
    case browser_to_bridge_discriminator::TERMINAL_INPUT:
      type = BrowserToBridgeMessage::Type::TERMINAL_INPUT;
      break;
    case browser_to_bridge_discriminator::RESIZE:
      type = BrowserToBridgeMessage::Type::RESIZE;
      break;
    case browser_to_bridge_discriminator::SWITCH_ANONYMOUS_TRAY:
      type = BrowserToBridgeMessage::Type::SWITCH_ANONYMOUS_TRAY;
      break;
    case browser_to_bridge_discriminator::TOGGLE_WORKTREE_OVERLAY:
      type = BrowserToBridgeMessage::Type::TOGGLE_WORKTREE_OVERLAY;
      break;
    case browser_to_bridge_discriminator::TOGGLE_COMMAND_MODE:
      type = BrowserToBridgeMessage::Type::TOGGLE_COMMAND_MODE;
      break;
    case browser_to_bridge_discriminator::WORKTREE_PICKER_ACTION:
      type = BrowserToBridgeMessage::Type::WORKTREE_PICKER_ACTION;
      break;
    case browser_to_bridge_discriminator::OVERLAY_NAVIGATION:
      type = BrowserToBridgeMessage::Type::OVERLAY_NAVIGATION;
      break;
    default:
      return std::nullopt;
  }
  return BrowserToBridgeMessage{.type = type, .payload = message.substr(1U)};
}

std::string encode_bridge_to_browser_message(BridgeToBrowserMessage const& message) {
  std::string encoded(1U, discriminator_for_bridge_message(message.type));
  encoded.append(message.payload);
  return encoded;
}

}  // namespace moe::bridge::protocol
