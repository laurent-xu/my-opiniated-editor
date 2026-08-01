#include <string>

#include "gtest/gtest.h"
#include "src/base/terminal_size.h"
#include "src/parent/terminal/screen/terminal_position.h"
#include "src/parent/terminal/screen/test/terminal_screen_snapshot_test_support.h"

namespace {

using moe::base::TerminalSize;
using moe::parent::TerminalPosition;
using moe::parent::test_support::expected_full_snapshot;
using moe::parent::test_support::render_snapshot;

constexpr TerminalSize ONE_LINE{.rows = 1, .cols = 5};

TEST(TerminalScreenColorSnapshotTest, CharacterizesIndexedEraseFill) {
  EXPECT_EQ(render_snapshot(ONE_LINE, {"\x1b[48;5;42mX\x1b[K"}),
            expected_full_snapshot("\x1b[1;1H\x1b[0;39;48;5;42mX\x1b[K\x1b[0m",
                                   TerminalPosition{.row = 0, .column = 1}));
}

TEST(TerminalScreenColorSnapshotTest, CharacterizesRgbEraseFill) {
  EXPECT_EQ(render_snapshot(ONE_LINE, {"\x1b[48;2;1;2;3mX\x1b[K"}),
            expected_full_snapshot("\x1b[1;1H\x1b[0;39;48;2;1;2;3mX\x1b[K\x1b[0m",
                                   TerminalPosition{.row = 0, .column = 1}));
}

TEST(TerminalScreenColorSnapshotTest, ExplicitCellBackgroundTakesPrecedenceOverEraseFill) {
  std::string const input = "\x1b[48;5;42m\x1b[2K\x1b[0m\x1b[3G\x1b[38;5;11;48;5;99mX\x1b[0m";
  std::string const body =
      "\x1b[1;1H\x1b[0;39;48;5;42m  \x1b[0;38;5;11;48;5;99mX"
      "\x1b[0;39;48;5;42m\x1b[K\x1b[0m";
  EXPECT_EQ(render_snapshot(ONE_LINE, {input}),
            expected_full_snapshot(body, TerminalPosition{.row = 0, .column = 3}));
}

TEST(TerminalScreenColorSnapshotTest, NonuniformTrailingFillsRenderSpacesInsteadOfEraseLine) {
  std::string const input = "\x1b[48;5;42m\x1b[2K\x1b[4G\x1b[48;5;43m\x1b[K";
  std::string const body = "\x1b[1;1H\x1b[0;39;48;5;42m   \x1b[0;39;48;5;43m  \x1b[0m";
  EXPECT_EQ(render_snapshot(ONE_LINE, {input}),
            expected_full_snapshot(body, TerminalPosition{.row = 0, .column = 3}));
}

}  // namespace
