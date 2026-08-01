#include <string>

#include "gtest/gtest.h"
#include "src/parent/terminal/screen/terminal_screen.h"

namespace {

TEST(TerminalScreenTest, RedrawPreservesIndexedBackgroundColor) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest("\x1b[48;5;42mcolored\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;42mcolored"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsTrailingColoredBackgroundCells) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 5});

  screen.ingest("\x1b[48;5;42m     \x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;42m     \x1b[0m"), std::string::npos);
}

}  // namespace
