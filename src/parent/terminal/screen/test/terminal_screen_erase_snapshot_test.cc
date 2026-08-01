#include <string>
#include <string_view>

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
constexpr TerminalSize THREE_LINES{.rows = 3, .cols = 5};
constexpr std::string_view INDEXED_FILL = "\x1b[0;39;48;5;42m";

std::string positioned_line(int const row, std::string const& contents) {
  return "\x1b[" + std::to_string(row + 1) + ";1H" + contents;
}

TEST(TerminalScreenEraseSnapshotTest, CharacterizesEraseLineModeZero) {
  EXPECT_EQ(render_snapshot(ONE_LINE, {"ab\x1b[48;5;42m\x1b[0K"}),
            expected_full_snapshot("\x1b[1;1Hab" + std::string(INDEXED_FILL) + "\x1b[K\x1b[0m",
                                   TerminalPosition{.row = 0, .column = 2}));
}

TEST(TerminalScreenEraseSnapshotTest, CharacterizesEraseLineModeOne) {
  EXPECT_EQ(render_snapshot(ONE_LINE, {"abcde\x1b[3G\x1b[48;5;42m\x1b[1K"}),
            expected_full_snapshot("\x1b[1;1H" + std::string(INDEXED_FILL) + "   \x1b[0mde",
                                   TerminalPosition{.row = 0, .column = 2}));
}

TEST(TerminalScreenEraseSnapshotTest, CharacterizesEraseLineModeTwo) {
  EXPECT_EQ(render_snapshot(ONE_LINE, {"abcde\x1b[3G\x1b[48;5;42m\x1b[2K"}),
            expected_full_snapshot("\x1b[1;1H" + std::string(INDEXED_FILL) + "\x1b[K\x1b[0m",
                                   TerminalPosition{.row = 0, .column = 2}));
}

std::string populated_screen_then(std::string const& command) {
  return "11111\x1b[2;1H22222\x1b[3;1H33333\x1b[2;3H\x1b[48;5;42m" + command;
}

TEST(TerminalScreenEraseSnapshotTest, CharacterizesEraseDisplayModeZero) {
  std::string const body = positioned_line(0, "11111") +
                           positioned_line(1, "22" + std::string(INDEXED_FILL) + "\x1b[K\x1b[0m") +
                           positioned_line(2, std::string(INDEXED_FILL) + "\x1b[K\x1b[0m");
  EXPECT_EQ(render_snapshot(THREE_LINES, {populated_screen_then("\x1b[0J")}),
            expected_full_snapshot(body, TerminalPosition{.row = 1, .column = 2}));
}

TEST(TerminalScreenEraseSnapshotTest, CharacterizesEraseDisplayModeOne) {
  std::string const body = positioned_line(0, std::string(INDEXED_FILL) + "\x1b[K\x1b[0m") +
                           positioned_line(1, std::string(INDEXED_FILL) + "   \x1b[0m22") +
                           positioned_line(2, "33333");
  EXPECT_EQ(render_snapshot(THREE_LINES, {populated_screen_then("\x1b[1J")}),
            expected_full_snapshot(body, TerminalPosition{.row = 1, .column = 2}));
}

TEST(TerminalScreenEraseSnapshotTest, CharacterizesEraseDisplayModeTwo) {
  std::string const filled_line = std::string(INDEXED_FILL) + "\x1b[K\x1b[0m";
  std::string const body = positioned_line(0, filled_line) + positioned_line(1, filled_line) +
                           positioned_line(2, filled_line);
  EXPECT_EQ(render_snapshot(THREE_LINES, {populated_screen_then("\x1b[2J")}),
            expected_full_snapshot(body, TerminalPosition{.row = 1, .column = 2}));
}

TEST(TerminalScreenEraseSnapshotTest, CharacterizesEraseDisplayModeThree) {
  std::string const body = positioned_line(0, std::string(INDEXED_FILL) + "11111\x1b[0m") +
                           positioned_line(1, std::string(INDEXED_FILL) + "22222\x1b[0m") +
                           positioned_line(2, std::string(INDEXED_FILL) + "33333\x1b[0m");
  EXPECT_EQ(render_snapshot(THREE_LINES, {populated_screen_then("\x1b[3J")}),
            expected_full_snapshot(body, TerminalPosition{.row = 1, .column = 2}));
}

}  // namespace
