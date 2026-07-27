#include "src/parent/terminal_screen.h"

#include <string>

#include "gtest/gtest.h"

namespace {

TEST(TerminalScreenTest, RedrawIncludesScrollbackAndVisibleScreen) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest("one\r\ntwo\r\nthree\r\nfour");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("\x1b[H\x1b[2J"), std::string::npos);
  EXPECT_NE(redraw.find("one"), std::string::npos);
  EXPECT_NE(redraw.find("two"), std::string::npos);
  EXPECT_NE(redraw.find("three"), std::string::npos);
  EXPECT_NE(redraw.find("four"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawUsesTerminalStateInsteadOfRawByteReplay) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest("old text");
  screen.ingest("\x1b[2J\x1b[Hnew text");

  std::string const redraw = screen.render_snapshot();
  EXPECT_EQ(redraw.find("old text"), std::string::npos);
  EXPECT_NE(redraw.find("new text"), std::string::npos);
}

}  // namespace
