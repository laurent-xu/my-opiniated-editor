#pragma once

#include <variant>

#include "src/parent/input/event/command_input_event.h"
#include "src/parent/input/event/literal_input_event.h"

namespace moe::parent {

using ParentInputEvent = std::variant<LiteralInputEvent, CommandInputEvent>;

}  // namespace moe::parent
