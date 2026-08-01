#pragma once

#include <cstdint>

namespace moe::parent {

enum class RepositoryRootState : std::uint8_t {
  EMPTY,
  BARE_ROOT,
  RECOVERABLE_BARE_ROOT,
};

}  // namespace moe::parent
