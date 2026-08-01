#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "src/parent/input/parent_input_event.h"

namespace moe::parent {

class ParentInputDecoder {
 public:
  [[nodiscard]] std::vector<ParentInputEvent> consume(std::string_view bytes);

 private:
  enum class State : std::uint8_t {
    LITERAL_INPUT,
    COMMAND_BYTE,
  };

  State state = State::LITERAL_INPUT;
  std::uint8_t pending_command_prefix = 0;
};

}  // namespace moe::parent
