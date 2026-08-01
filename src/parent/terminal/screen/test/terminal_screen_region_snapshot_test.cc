#include <string>

#include "gtest/gtest.h"
#include "src/base/terminal_size.h"
#include "src/parent/terminal/screen/terminal_position.h"
#include "src/parent/terminal/screen/terminal_screen.h"

namespace {

using moe::base::TerminalSize;
using moe::parent::TerminalPosition;
using moe::parent::TerminalScreen;

TEST(TerminalScreenRegionSnapshotTest, CharacterizesClippedRowsAndColumnsExactly) {
  TerminalScreen screen(TerminalSize{.rows = 4, .cols = 8});
  screen.ingest("\x1b[1;1Htop-wide\x1b[4;1Hbottom");

  EXPECT_EQ(screen.render_region_snapshot(TerminalPosition{.row = 4, .column = 0},
                                          TerminalSize{.rows = 2, .cols = 5}),
            "\x1b[?25l\x1b[?7l"
            "\x1b[5;1H\x1b[0m     \x1b[5;1Htop-w"
            "\x1b[6;1H\x1b[0m     "
            "\x1b[0m\x1b[?7h\x1b[?25l");
}

TEST(TerminalScreenRegionSnapshotTest, ClippedFillWritesSpacesWithoutEraseLine) {
  TerminalScreen screen(TerminalSize{.rows = 2, .cols = 5});
  screen.ingest("\x1b[48;5;42mX\x1b[K\x1b[0m");

  EXPECT_EQ(screen.render_region_snapshot(TerminalPosition{.row = 2, .column = 3},
                                          TerminalSize{.rows = 1, .cols = 3}),
            "\x1b[?25l\x1b[?7l"
            "\x1b[3;4H\x1b[0m   \x1b[3;4H\x1b[0;39;48;5;42mX  \x1b[0m"
            "\x1b[0m\x1b[?7h\x1b[?25l");
}

}  // namespace
