#pragma once

#include <cstdint>

namespace moe::parent {

enum class TrayActionIntent : std::uint8_t {
  CLEAR,
  REMOVE,
};

}  // namespace moe::parent
