#pragma once

#include <cstdint>

namespace moe::parent {

enum class TrayActionKind : std::uint8_t {
  CLEAR,
  REMOVE,
};

}  // namespace moe::parent
