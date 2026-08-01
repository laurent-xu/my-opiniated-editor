#pragma once

#include <string_view>

namespace moe::bridge::protocol {

struct BrowserTerminalInput {
  std::string_view bytes;
};

}  // namespace moe::bridge::protocol
