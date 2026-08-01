#pragma once

#include <optional>
#include <string_view>

#include "src/bridge/protocol/browser_application_message.h"

namespace moe::bridge::protocol {

[[nodiscard]] std::optional<BrowserApplicationMessage> parse_browser_application_message(
    std::string_view message);

}  // namespace moe::bridge::protocol
