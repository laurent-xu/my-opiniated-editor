#pragma once

#include <cstdint>

namespace moe::process {

enum class StandardOutputMode : std::uint8_t {
  INHERIT,
  CAPTURE,
};

}  // namespace moe::process
