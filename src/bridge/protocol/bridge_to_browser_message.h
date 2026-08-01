#pragma once

#include <cstdint>
#include <string_view>

namespace moe::bridge::protocol {

struct BridgeToBrowserMessage {
  enum class Type : std::uint8_t {
    TERMINAL_OUTPUT,
    PARENT_STATUS,
  };

  Type type;
  std::string_view payload;
};

}  // namespace moe::bridge::protocol
