#pragma once

#include <cstddef>
#include <string_view>

#include "src/parent/terminal/control_sequence_introducer_parse_result.h"
#include "src/parent/terminal/control_sequence_introducer_sequence.h"

namespace moe::parent {

[[nodiscard]] ControlSequenceIntroducerParseResult parse_control_sequence_introducer_sequence(
    std::string_view bytes, std::size_t start);

[[nodiscard]] bool is_erase_sequence(ControlSequenceIntroducerSequence sequence);

}  // namespace moe::parent
