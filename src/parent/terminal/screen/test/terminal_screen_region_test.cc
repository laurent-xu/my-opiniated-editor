#include <string>

#include "gtest/gtest.h"
#include "src/parent/terminal/screen/terminal_screen.h"

namespace {

TEST(TerminalScreenTest, RegionRedrawOffsetsRowsAndDoesNotEraseOutsideRegion) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 2, .cols = 5});
  screen.ingest("\x1b[48;5;42m>\x1b[K\x1b[0m");

  std::string const redraw =
      screen.render_region_snapshot(moe::parent::TerminalPosition{.row = 4, .column = 2});

  EXPECT_NE(redraw.find("\x1b[5;3H"), std::string::npos);
  EXPECT_NE(redraw.find("48;5;42m>    \x1b[0m"), std::string::npos);
  EXPECT_EQ(redraw.find("\x1b[2J"), std::string::npos);
  EXPECT_EQ(redraw.find("\x1b[K"), std::string::npos);
}

TEST(TerminalScreenTest, ClippedRegionRedrawShowsTopRows) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 4, .cols = 8});
  screen.ingest("\x1b[1;1Htop\x1b[4;1Hbottom");

  std::string const redraw =
      screen.render_region_snapshot(moe::parent::TerminalPosition{.row = 4, .column = 0},
                                    moe::base::TerminalSize{.rows = 2, .cols = 8});

  EXPECT_NE(redraw.find("\x1b[5;1Htop"), std::string::npos);
  EXPECT_EQ(redraw.find("bottom"), std::string::npos);
  EXPECT_NE(redraw.find("\x1b[6;1H\x1b[0m        "), std::string::npos);
}

TEST(TerminalScreenTest, BlankRegionRedrawIsCompletelyDark) {
  std::string const redraw = moe::parent::TerminalScreen::render_blank_region_snapshot(
      moe::parent::TerminalPosition{.row = 2, .column = 1},
      moe::base::TerminalSize{.rows = 2, .cols = 4});

  EXPECT_NE(redraw.find("\x1b[3;2H\x1b[0;48;5;232m    "), std::string::npos);
  EXPECT_NE(redraw.find("\x1b[4;2H\x1b[0;48;5;232m    "), std::string::npos);
}

}  // namespace
