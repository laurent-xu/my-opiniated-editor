#pragma once

#include <vterm.h>

#include <string>

#include "src/parent/terminal/snapshot/terminal_color.h"

namespace moe::parent {

struct CellStyle {
  bool bold = false;
  bool italic = false;
  bool blink = false;
  bool reverse = false;
  bool strike = false;
  int underline = VTERM_UNDERLINE_OFF;
  TerminalColor foreground;
  TerminalColor background;

  [[nodiscard]] bool operator==(CellStyle const& other) const {
    return bold == other.bold && italic == other.italic && blink == other.blink &&
           reverse == other.reverse && strike == other.strike && underline == other.underline &&
           foreground == other.foreground && background == other.background;
  }

  [[nodiscard]] bool is_default() const { return *this == CellStyle{}; }
};

[[nodiscard]] CellStyle cell_style_from(VTermScreenCell const& cell);
[[nodiscard]] CellStyle current_style_from_state(VTermState const* state);
[[nodiscard]] std::string sgr_sequence(CellStyle const& style);

}  // namespace moe::parent
