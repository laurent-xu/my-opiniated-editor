#pragma once

#include <cstdint>

namespace moe::parent {

enum class WorktreeProvisionKind : std::uint8_t {
  ADOPTED,
  CREATED,
};

}  // namespace moe::parent
