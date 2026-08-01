#pragma once

#include <vterm.h>

namespace moe::parent {

struct TerminalRectMove {
  VTermRect dest;
  VTermRect src;
};

}  // namespace moe::parent
