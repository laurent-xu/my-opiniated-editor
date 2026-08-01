#pragma once

#include <string>

#include "src/parent/status/parent_status.h"

namespace moe::parent {

[[nodiscard]] std::string serialize_parent_status(ParentStatus const& status);

}  // namespace moe::parent
