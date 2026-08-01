#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "src/bridge/protocol/bridge_to_browser_message.h"
#include "src/bridge/protocol/browser_to_bridge_message.h"

namespace moe::bridge::protocol {

[[nodiscard]] std::optional<BrowserToBridgeMessage> decode_browser_to_bridge_message(
    std::string_view message);

[[nodiscard]] std::string encode_bridge_to_browser_message(BridgeToBrowserMessage const& message);

}  // namespace moe::bridge::protocol
