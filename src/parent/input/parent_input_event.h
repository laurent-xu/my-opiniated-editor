#pragma once

#include <variant>

#include "src/parent/input/command_input_event.h"
#include "src/parent/input/literal_input_event.h"

namespace moe::parent {

using ParentInputEvent = std::variant<LiteralInputEvent, CommandInputEvent>;

}  // namespace moe::parent
