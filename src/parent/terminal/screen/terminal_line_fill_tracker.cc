#include "src/parent/terminal/screen/terminal_line_fill_tracker.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "src/base/terminal_size.h"
#include "src/parent/terminal/screen/terminal_rect_move.h"
#include "src/parent/terminal/snapshot/terminal_cell_style.h"
#include "src/parent/terminal/snapshot/terminal_row_fill_styles.h"

namespace moe::parent {
namespace {

int clamp_to_screen(int const value, int const upper_bound) {
  return std::clamp(value, 0, std::max(0, upper_bound - 1));
}

}  // namespace

TerminalLineFillTracker::TerminalLineFillTracker(base::TerminalSize const initial_size) {
  resize(initial_size);
}

void TerminalLineFillTracker::resize(base::TerminalSize const next_size) {
  size = next_size;
  row_styles.assign(static_cast<std::size_t>(size.rows),
                    TerminalRowFillStyles(static_cast<std::size_t>(size.cols)));
}

void TerminalLineFillTracker::clear_all() {
  for (TerminalRowFillStyles& row : row_styles) {
    std::ranges::fill(row, std::nullopt);
  }
}

void TerminalLineFillTracker::record_range(int const row, int const start_col, int const end_col,
                                           CellStyle const& style) {
  if (row < 0 || row >= size.rows || end_col < 0 || start_col >= size.cols) {
    return;
  }

  int const clamped_start = clamp_to_screen(start_col, size.cols);
  int const clamped_end = clamp_to_screen(end_col, size.cols);
  TerminalRowFillStyles& row_style = row_styles[static_cast<std::size_t>(row)];
  for (int col = clamped_start; col <= clamped_end; ++col) {
    std::optional<CellStyle>& cell_style = row_style[static_cast<std::size_t>(col)];
    if (style.is_default()) {
      cell_style.reset();
    } else {
      cell_style = style;
    }
  }
}

void TerminalLineFillTracker::record_rows(int const start_row, int const end_row,
                                          CellStyle const& style) {
  if (end_row < 0 || start_row >= size.rows) {
    return;
  }

  int const clamped_start = clamp_to_screen(start_row, size.rows);
  int const clamped_end = clamp_to_screen(end_row, size.rows);
  for (int row = clamped_start; row <= clamped_end; ++row) {
    record_range(row, 0, size.cols - 1, style);
  }
}

void TerminalLineFillTracker::move_rect(TerminalRectMove const& move) {
  std::vector<TerminalRowFillStyles> const old_row_styles = row_styles;
  VTermRect const dest = move.dest;
  VTermRect const src = move.src;
  int const height = std::min(dest.end_row - dest.start_row, src.end_row - src.start_row);
  int const width = std::min(dest.end_col - dest.start_col, src.end_col - src.start_col);
  if (height <= 0 || width <= 0) {
    return;
  }

  for (int row_offset = 0; row_offset < height; ++row_offset) {
    int const dest_row = dest.start_row + row_offset;
    int const src_row = src.start_row + row_offset;
    if (dest_row < 0 || dest_row >= size.rows) {
      continue;
    }

    TerminalRowFillStyles& dest_row_styles = row_styles[static_cast<std::size_t>(dest_row)];
    for (int col_offset = 0; col_offset < width; ++col_offset) {
      int const dest_col = dest.start_col + col_offset;
      int const src_col = src.start_col + col_offset;
      if (dest_col < 0 || dest_col >= size.cols) {
        continue;
      }

      std::optional<CellStyle>& dest_style = dest_row_styles[static_cast<std::size_t>(dest_col)];
      if (src_row < 0 || src_row >= size.rows || src_col < 0 || src_col >= size.cols) {
        dest_style.reset();
        continue;
      }
      dest_style =
          old_row_styles[static_cast<std::size_t>(src_row)][static_cast<std::size_t>(src_col)];
    }
  }

  clear_source_cells_outside_dest(move);
}

TerminalRowFillStyles const* TerminalLineFillTracker::row(int const row) const {
  if (row < 0 || row >= size.rows) {
    return nullptr;
  }
  return &row_styles[static_cast<std::size_t>(row)];
}

void TerminalLineFillTracker::clear_source_cells_outside_dest(TerminalRectMove const& move) {
  VTermRect const dest = move.dest;
  VTermRect const src = move.src;
  for (int row = src.start_row; row < src.end_row; ++row) {
    if (row < 0 || row >= size.rows) {
      continue;
    }

    TerminalRowFillStyles& row_style = row_styles[static_cast<std::size_t>(row)];
    for (int col = src.start_col; col < src.end_col; ++col) {
      if (col < 0 || col >= size.cols ||
          vterm_rect_contains(dest, VTermPos{.row = row, .col = col}) != 0) {
        continue;
      }
      row_style[static_cast<std::size_t>(col)].reset();
    }
  }
}

}  // namespace moe::parent
