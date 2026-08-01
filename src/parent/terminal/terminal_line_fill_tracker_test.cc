#include "src/parent/terminal/terminal_line_fill_tracker.h"

#include <cstddef>
#include <initializer_list>
#include <optional>

#include "gtest/gtest.h"
#include "src/base/terminal_size.h"
#include "src/parent/terminal/snapshot/terminal_cell_style.h"
#include "src/parent/terminal/snapshot/terminal_color.h"
#include "src/parent/terminal/snapshot/terminal_color_kind.h"
#include "src/parent/terminal/terminal_rect_move.h"

namespace {

using moe::base::TerminalSize;
using moe::parent::CellStyle;
using moe::parent::TerminalColor;
using moe::parent::TerminalColorKind;
using moe::parent::TerminalLineFillTracker;
using moe::parent::TerminalRectMove;

CellStyle indexed_background(int const index) {
  CellStyle style;
  style.background = TerminalColor{.kind = TerminalColorKind::INDEXED, .index = index};
  return style;
}

void expect_row(TerminalLineFillTracker const& tracker, int const row,
                std::initializer_list<std::optional<int>> const expected_indexes) {
  auto const* const styles = tracker.row(row);
  ASSERT_NE(styles, nullptr);
  ASSERT_EQ(styles->size(), expected_indexes.size());
  std::size_t column = 0;
  for (std::optional<int> const expected_index : expected_indexes) {
    std::optional<CellStyle> const& style = (*styles)[column++];
    if (!expected_index.has_value()) {
      EXPECT_FALSE(style.has_value());
      continue;
    }
    if (!style.has_value()) {
      ADD_FAILURE() << "expected a recorded fill style";
      continue;
    }
    EXPECT_EQ(style->background.kind, TerminalColorKind::INDEXED);
    EXPECT_EQ(style->background.index, expected_index.value());
  }
}

TEST(TerminalLineFillTrackerTest, ResizeReinitializesEveryCell) {
  TerminalLineFillTracker tracker(TerminalSize{.rows = 2, .cols = 3});
  tracker.record_rows(0, 1, indexed_background(7));

  tracker.resize(TerminalSize{.rows = 1, .cols = 2});

  expect_row(tracker, 0, {std::nullopt, std::nullopt});
  EXPECT_EQ(tracker.row(1), nullptr);
}

TEST(TerminalLineFillTrackerTest, ClearAllRemovesEveryRecordedStyle) {
  TerminalLineFillTracker tracker(TerminalSize{.rows = 2, .cols = 2});
  tracker.record_rows(0, 1, indexed_background(7));

  tracker.clear_all();

  expect_row(tracker, 0, {std::nullopt, std::nullopt});
  expect_row(tracker, 1, {std::nullopt, std::nullopt});
}

TEST(TerminalLineFillTrackerTest, RangeAndRowRecordingClampToTheScreen) {
  TerminalLineFillTracker tracker(TerminalSize{.rows = 3, .cols = 4});

  tracker.record_rows(-2, 0, indexed_background(1));
  tracker.record_range(1, -2, 2, indexed_background(2));
  tracker.record_rows(2, 5, indexed_background(3));

  expect_row(tracker, 0, {1, 1, 1, 1});
  expect_row(tracker, 1, {2, 2, 2, std::nullopt});
  expect_row(tracker, 2, {3, 3, 3, 3});
}

TEST(TerminalLineFillTrackerTest, DefaultStyleClearsOnlyTheRecordedRange) {
  TerminalLineFillTracker tracker(TerminalSize{.rows = 1, .cols = 4});
  tracker.record_rows(0, 0, indexed_background(7));

  tracker.record_range(0, 1, 2, CellStyle{});

  expect_row(tracker, 0, {7, std::nullopt, std::nullopt, 7});
}

TEST(TerminalLineFillTrackerTest, OverlappingMoveCopiesFromTheOriginalCells) {
  TerminalLineFillTracker tracker(TerminalSize{.rows = 1, .cols = 5});
  for (int column = 0; column < 5; ++column) {
    tracker.record_range(0, column, column, indexed_background(column + 1));
  }

  tracker.move_rect(TerminalRectMove{
      .dest = VTermRect{.start_row = 0, .end_row = 1, .start_col = 1, .end_col = 5},
      .src = VTermRect{.start_row = 0, .end_row = 1, .start_col = 0, .end_col = 4},
  });

  expect_row(tracker, 0, {std::nullopt, 1, 2, 3, 4});
}

TEST(TerminalLineFillTrackerTest, MoveCopiesDestinationAndClearsDisjointSource) {
  TerminalLineFillTracker tracker(TerminalSize{.rows = 2, .cols = 4});
  for (int column = 0; column < 4; ++column) {
    tracker.record_range(0, column, column, indexed_background(column + 1));
  }
  tracker.record_rows(1, 1, indexed_background(9));

  tracker.move_rect(TerminalRectMove{
      .dest = VTermRect{.start_row = 1, .end_row = 2, .start_col = 0, .end_col = 4},
      .src = VTermRect{.start_row = 0, .end_row = 1, .start_col = 0, .end_col = 4},
  });

  expect_row(tracker, 0, {std::nullopt, std::nullopt, std::nullopt, std::nullopt});
  expect_row(tracker, 1, {1, 2, 3, 4});
}

}  // namespace
