#pragma once

#include <vterm.h>

#include <string>

#include "src/parent/terminal/snapshot/terminal_row_fill_styles.h"

namespace moe::parent {

[[nodiscard]] std::string render_terminal_snapshot_line(
    int columns, VTermScreenCell const* cells,
    TerminalRowFillStyles const* row_fill_styles = nullptr, bool allow_erase_to_end_of_line = true);

}  // namespace moe::parent
