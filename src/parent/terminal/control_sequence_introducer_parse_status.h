#pragma once

#include <cstdint>

namespace moe::parent {

enum class ControlSequenceIntroducerParseStatus : std::uint8_t {
  NOT_CONTROL_SEQUENCE_INTRODUCER,
  INCOMPLETE,
  COMPLETE
};

}  // namespace moe::parent
