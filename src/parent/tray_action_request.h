#pragma once

#include <cstdint>

#include "src/parent/tray/tray_id.h"

namespace moe::parent {

enum class TrayActionKind : std::uint8_t {
  CLEAR,
  REMOVE,
};

struct TrayActionRequest {
  TrayActionKind kind;
  TrayId tray_id;
};

}  // namespace moe::parent
