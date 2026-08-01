#include "src/parent/terminal/snapshot/terminal_snapshot_line.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"
#include "src/parent/terminal/snapshot/terminal_cell_style.h"
#include "src/parent/terminal/snapshot/terminal_color.h"
#include "src/parent/terminal/snapshot/terminal_color_kind.h"
#include "src/parent/terminal/snapshot/terminal_row_fill_styles.h"

namespace {

using moe::parent::CellStyle;
using moe::parent::render_terminal_snapshot_line;
using moe::parent::TerminalColor;
using moe::parent::TerminalColorKind;
using moe::parent::TerminalRowFillStyles;

VTermScreenCell default_cell(std::uint32_t const codepoint = 0) {
  VTermScreenCell cell{};
  cell.chars[0] = codepoint;
  cell.width = 1;
  cell.fg.type = VTERM_COLOR_DEFAULT_FG;
  cell.bg.type = VTERM_COLOR_DEFAULT_BG;
  return cell;
}

VTermScreenCell wide_cell(std::uint32_t const codepoint) {
  VTermScreenCell cell = default_cell(codepoint);
  cell.width = 2;
  return cell;
}

VTermScreenCell continuation_cell() {
  VTermScreenCell cell = default_cell();
  cell.width = 0;
  return cell;
}

CellStyle indexed_background(int const index) {
  CellStyle style;
  style.background = TerminalColor{.kind = TerminalColorKind::INDEXED, .index = index};
  return style;
}

TEST(TerminalSnapshotLineTest, SkipsWideCellContinuationAndPreservesTrailingFill) {
  std::array<VTermScreenCell, 4> cells{
      wide_cell(0x754CU),
      continuation_cell(),
      default_cell('X'),
      default_cell(),
  };
  TerminalRowFillStyles fill_styles(cells.size());
  fill_styles[3] = indexed_background(42);

  EXPECT_EQ(
      render_terminal_snapshot_line(static_cast<int>(cells.size()), cells.data(), &fill_styles),
      "\xE7\x95\x8C"
      "X\x1b[0;39;48;5;42m\x1b[K\x1b[0m");
}

TEST(TerminalSnapshotLineTest, SerializesCombiningCodepointsFromOneCell) {
  std::array<VTermScreenCell, 1> cells{default_cell('e')};
  cells[0].chars[1] = 0x0301U;

  EXPECT_EQ(render_terminal_snapshot_line(static_cast<int>(cells.size()), cells.data()),
            "e\xCC\x81");
}

}  // namespace
