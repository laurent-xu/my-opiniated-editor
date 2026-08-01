#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "src/base/terminal_size.h"
#include "src/parent/terminal/screen/terminal_position.h"
#include "src/parent/terminal/screen/terminal_screen.h"

namespace moe::parent::test_support {

inline std::string cursor_position_sequence(TerminalPosition const position) {
  return "\x1b[" + std::to_string(position.row + 1) + ";" + std::to_string(position.column + 1) +
         "H";
}

inline std::string expected_full_snapshot(std::string_view const body,
                                          TerminalPosition const cursor,
                                          bool const alternate_screen = false) {
  std::string output(alternate_screen ? "\x1b[?1049h" : "\x1b[?1049l");
  output.append("\x1b[?5l\x1b[?25h\x1b[0m\x1b[H\x1b[2J");
  if (!alternate_screen) {
    output.append("\x1b[3J");
  }
  output.append("\x1b[?7l");
  output.append(body);
  output.append(cursor_position_sequence(cursor));
  output.append("\x1b[?7h");
  return output;
}

inline std::string render_snapshot(base::TerminalSize const size,
                                   std::initializer_list<std::string_view> const chunks) {
  TerminalScreen screen(size);
  for (std::string_view const chunk : chunks) {
    screen.ingest(chunk);
  }
  return screen.render_snapshot();
}

}  // namespace moe::parent::test_support
