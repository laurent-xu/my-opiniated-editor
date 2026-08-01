#pragma once

#include "src/parent/terminal/terminal_color_kind.h"

namespace moe::parent {

struct TerminalColor {
  TerminalColorKind kind = TerminalColorKind::DEFAULT;
  int index = 0;
  int red = 0;
  int green = 0;
  int blue = 0;

  [[nodiscard]] bool operator==(TerminalColor const& other) const {
    return kind == other.kind && index == other.index && red == other.red && green == other.green &&
           blue == other.blue;
  }

  [[nodiscard]] bool is_default() const { return kind == TerminalColorKind::DEFAULT; }
};

}  // namespace moe::parent
