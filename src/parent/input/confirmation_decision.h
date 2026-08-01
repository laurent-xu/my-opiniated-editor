#pragma once

#include <cstdint>

namespace moe::parent {

enum class ConfirmationDecision : std::uint8_t {
  CONFIRM,
  CANCEL,
};

}  // namespace moe::parent
