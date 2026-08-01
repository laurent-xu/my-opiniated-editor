#pragma once

#include <cstdint>

namespace moe::parent {

enum class OverlayNavigation : std::uint8_t {
  UP,
  DOWN,
  RIGHT,
  LEFT,
  TAB,
  BACKTAB,
  ENTER,
};

}  // namespace moe::parent
