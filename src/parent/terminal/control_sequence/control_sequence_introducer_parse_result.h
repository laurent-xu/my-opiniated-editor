#pragma once

#include "src/parent/terminal/control_sequence/control_sequence_introducer_parse_status.h"
#include "src/parent/terminal/control_sequence/control_sequence_introducer_sequence.h"

namespace moe::parent {

struct ControlSequenceIntroducerParseResult {
  ControlSequenceIntroducerParseStatus status =
      ControlSequenceIntroducerParseStatus::NOT_CONTROL_SEQUENCE_INTRODUCER;
  ControlSequenceIntroducerSequence sequence;
};

}  // namespace moe::parent
