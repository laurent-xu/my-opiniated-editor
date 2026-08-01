#pragma once

#include <vector>

#include "src/base/terminal_size.h"
#include "src/parent/terminal/screen/terminal_rect_move.h"
#include "src/parent/terminal/snapshot/terminal_cell_style.h"
#include "src/parent/terminal/snapshot/terminal_row_fill_styles.h"

namespace moe::parent {

class TerminalLineFillTracker {
 public:
  explicit TerminalLineFillTracker(base::TerminalSize initial_size);

  void resize(base::TerminalSize next_size);
  void clear_all();
  void record_range(int row, int start_col, int end_col, CellStyle const& style);
  void record_rows(int start_row, int end_row, CellStyle const& style);
  void move_rect(TerminalRectMove const& move);
  [[nodiscard]] TerminalRowFillStyles const* row(int row) const;

 private:
  void clear_source_cells_outside_dest(TerminalRectMove const& move);

  base::TerminalSize size{};
  std::vector<TerminalRowFillStyles> row_styles;
};

}  // namespace moe::parent
