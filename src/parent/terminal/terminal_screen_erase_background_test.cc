#include <string>

#include "gtest/gtest.h"
#include "src/parent/terminal/terminal_screen.h"

namespace {

TEST(TerminalScreenTest, RedrawKeepsBackgroundFromEraseToEndOfLine) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 10});

  screen.ingest("\x1b[48;5;242m>\x1b[K\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m>\x1b[K\x1b[0m"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsBackgroundFromEraseWholeLine) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 10});

  screen.ingest("plain\x1b[48;5;242m\x1b[2K>\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m     >\x1b[K\x1b[0m"), std::string::npos);
  EXPECT_EQ(redraw.find("plain"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsBackgroundFromEraseBeforePromptText) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 12});

  screen.ingest(
      "\x1b[2;1H\x1b[48;5;242m\x1b[2K\x1b[0m\x1b[5G"
      "\x1b[48;5;242m>\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m    >\x1b[K\x1b[0m"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsEraseBackgroundAfterSameSizeResize) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 12});

  screen.ingest("\x1b[2;1H\x1b[48;5;235m>\x1b[K\x1b[0m");
  screen.resize(moe::base::TerminalSize{.rows = 3, .cols = 12});

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;235m>\x1b[K\x1b[0m"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawInheritsEraseBackgroundUnderPromptText) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 12});

  screen.ingest(
      "\x1b[2;1H\x1b[48;5;242m\x1b[2K\x1b[0m\x1b[5G"
      "\x1b[38;5;11m>\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m    "), std::string::npos);
  EXPECT_NE(redraw.find("38;5;11;48;5;242m>"), std::string::npos);
  EXPECT_NE(redraw.find("48;5;242m\x1b[K"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsBackgroundFromSplitEraseToEndOfLine) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 10});

  screen.ingest("\x1b[48;5;242m>");
  screen.ingest("\x1b[");
  screen.ingest("K\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m>\x1b[K\x1b[0m"), std::string::npos);
}

TEST(TerminalScreenTest, DefaultEraseClearsRememberedBackgroundFill) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 10});

  screen.ingest("\x1b[48;5;242m\x1b[2K\x1b[0m");
  screen.ingest("\x1b[H\x1b[2K");

  std::string const redraw = screen.render_snapshot();
  EXPECT_EQ(redraw.find("48;5;242m\x1b[K"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawMovesRememberedEraseBackgroundWhenScrollRegionScrolls) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 4, .cols = 8});

  screen.ingest("\x1b[3;1H\x1b[48;5;242mx\x1b[K\x1b[0m");
  screen.ingest("\x1b[2;4r\x1b[4;1H\n\x1b[r");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("\x1b[2;1H\x1b[0;39;48;5;242mx\x1b[K\x1b[0m"), std::string::npos);
  EXPECT_EQ(redraw.find("\x1b[3;1H\x1b[48;5;242m"), std::string::npos);
}

}  // namespace
