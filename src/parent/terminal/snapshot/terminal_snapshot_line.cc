#include "src/parent/terminal/snapshot/terminal_snapshot_line.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "src/parent/terminal/snapshot/terminal_cell_style.h"

namespace moe::parent {
namespace {

constexpr std::string_view ERASE_TO_END_OF_LINE = "\x1b[K";
constexpr std::uint32_t UNICODE_REPLACEMENT_CHARACTER = 0xFFFDU;

struct CellRange {
  int first_col = 0;
  int last_col = 0;
};

struct SnapshotLineBounds {
  int last_rendered = -1;
  int last_text = -1;
};

void append_utf8(std::string& output, std::uint32_t const codepoint) {
  if (codepoint == 0 || codepoint == UNICODE_REPLACEMENT_CHARACTER) {
    return;
  }
  if ((codepoint >= 0xD800U && codepoint <= 0xDFFFU) || codepoint > 0x10FFFFU) {
    return;
  }
  std::uint32_t const value = codepoint;
  if (value <= 0x7F) {
    output.push_back(static_cast<char>(value));
    return;
  }
  if (value <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
    output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    return;
  }
  if (value <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    return;
  }
  output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
  output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
  output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
  output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
}

bool cell_has_text(VTermScreenCell const& cell) { return cell.chars[0] != 0; }

std::optional<CellStyle> row_fill_style_at(TerminalRowFillStyles const* const row_fill_styles,
                                           int const col) {
  if (row_fill_styles == nullptr || col < 0 ||
      static_cast<std::size_t>(col) >= row_fill_styles->size()) {
    return std::nullopt;
  }
  return (*row_fill_styles)[static_cast<std::size_t>(col)];
}

CellStyle effective_cell_style(VTermScreenCell const& cell,
                               std::optional<CellStyle> const fill_style) {
  CellStyle const cell_style = cell_style_from(cell);
  if (!fill_style.has_value()) {
    return cell_style;
  }
  if (cell.width == 0 || (!cell_has_text(cell) && cell_style.is_default())) {
    return *fill_style;
  }
  if (fill_style->background.is_default() || !cell_style.background.is_default()) {
    return cell_style;
  }

  CellStyle merged_style = cell_style;
  merged_style.background = fill_style->background;
  return merged_style;
}

bool cell_should_be_rendered(VTermScreenCell const& cell, CellStyle const& style) {
  return cell.width != 0 && (cell_has_text(cell) || !style.is_default());
}

void append_cell_text(std::string& line, VTermScreenCell const& cell) {
  if (!cell_has_text(cell)) {
    line.push_back(' ');
    return;
  }
  for (std::uint32_t const character : cell.chars) {
    append_utf8(line, character);
  }
}

int last_rendered_cell_index(int const cols, VTermScreenCell const* const cells,
                             TerminalRowFillStyles const* const row_fill_styles) {
  int last_rendered = -1;
  for (int col = 0; col < cols; ++col) {
    if (cell_should_be_rendered(
            cells[col],
            effective_cell_style(cells[col], row_fill_style_at(row_fill_styles, col)))) {
      last_rendered = col;
    }
  }
  return last_rendered;
}

int last_text_cell_index(int const cols, VTermScreenCell const* const cells) {
  int last_text = -1;
  for (int col = 0; col < cols; ++col) {
    if (cells[col].width != 0 && cell_has_text(cells[col])) {
      last_text = col;
    }
  }
  return last_text;
}

bool blank_cells_have_same_style(CellRange const range, VTermScreenCell const* const cells,
                                 TerminalRowFillStyles const* const row_fill_styles,
                                 CellStyle const& style) {
  for (int col = range.first_col; col <= range.last_col; ++col) {
    VTermScreenCell const& cell = cells[col];
    if (cell.width == 0 || cell_has_text(cell) ||
        !(effective_cell_style(cell, row_fill_style_at(row_fill_styles, col)) == style)) {
      return false;
    }
  }
  return true;
}

bool should_erase_to_end_of_line(int const cols, VTermScreenCell const* const cells,
                                 TerminalRowFillStyles const* const row_fill_styles,
                                 SnapshotLineBounds const bounds) {
  if (bounds.last_rendered != cols - 1) {
    return false;
  }

  int const first_trailing_blank = bounds.last_text + 1;
  if (first_trailing_blank > bounds.last_rendered) {
    return false;
  }

  CellStyle const trailing_style = effective_cell_style(
      cells[first_trailing_blank], row_fill_style_at(row_fill_styles, first_trailing_blank));
  return !trailing_style.is_default() &&
         blank_cells_have_same_style(
             CellRange{.first_col = first_trailing_blank, .last_col = bounds.last_rendered}, cells,
             row_fill_styles, trailing_style);
}

}  // namespace

std::string render_terminal_snapshot_line(int const columns, VTermScreenCell const* const cells,
                                          TerminalRowFillStyles const* const row_fill_styles,
                                          bool const allow_erase_to_end_of_line) {
  int const last_rendered = last_rendered_cell_index(columns, cells, row_fill_styles);
  if (last_rendered < 0) {
    return "";
  }

  int const last_text = last_text_cell_index(columns, cells);
  bool const erase_to_end_of_line =
      allow_erase_to_end_of_line &&
      should_erase_to_end_of_line(
          columns, cells, row_fill_styles,
          SnapshotLineBounds{.last_rendered = last_rendered, .last_text = last_text});
  int const last_cell_to_write = erase_to_end_of_line ? last_text : last_rendered;

  std::string line;
  CellStyle current_style;
  for (int col = 0; col <= last_cell_to_write; ++col) {
    VTermScreenCell const& cell = cells[col];
    if (cell.width == 0) {
      continue;
    }
    CellStyle const next_style =
        effective_cell_style(cell, row_fill_style_at(row_fill_styles, col));
    if (!(next_style == current_style)) {
      line.append(sgr_sequence(next_style));
      current_style = next_style;
    }
    append_cell_text(line, cell);
  }
  if (erase_to_end_of_line) {
    CellStyle const erase_style = effective_cell_style(
        cells[last_text + 1], row_fill_style_at(row_fill_styles, last_text + 1));
    if (!(erase_style == current_style)) {
      line.append(sgr_sequence(erase_style));
      current_style = erase_style;
    }
    line.append(ERASE_TO_END_OF_LINE);
  }
  if (!current_style.is_default()) {
    line.append(sgr_sequence(CellStyle{}));
  }
  return line;
}

}  // namespace moe::parent
