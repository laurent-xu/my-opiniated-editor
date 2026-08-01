#pragma once

#include <cstddef>

namespace moe::parent {

struct ControlSequenceIntroducerSequence {
  std::size_t start = 0;
  std::size_t end = 0;
  char command = '\0';
  int mode = -1;
};

}  // namespace moe::parent
