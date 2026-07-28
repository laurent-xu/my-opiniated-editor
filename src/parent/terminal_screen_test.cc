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

TEST(TerminalScreenTest, RedrawDisablesAutowrapAroundSnapshotRows) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 2, .cols = 5});

  screen.ingest("abcde");

  std::string const redraw = screen.render_snapshot();
  std::size_t const disable_autowrap = redraw.find("\x1b[?7l");
  std::size_t const row_text = redraw.find("abcde");
  std::size_t const enable_autowrap = redraw.find("\x1b[?7h");

  ASSERT_NE(disable_autowrap, std::string::npos);
  ASSERT_NE(row_text, std::string::npos);
  ASSERT_NE(enable_autowrap, std::string::npos);
  EXPECT_LT(disable_autowrap, row_text);
  EXPECT_LT(row_text, enable_autowrap);
}

TEST(TerminalScreenTest, RedrawPreservesUtf8SplitAcrossInputChunks) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest(std::string("\xE2\x94", 2));
  screen.ingest(std::string("\x80", 1));

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("\xE2\x94\x80"), std::string::npos);
  EXPECT_EQ(redraw.find("\xEF\xBF\xBD"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawDoesNotEmitReplacementGlyphForInvalidUtf8) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest(std::string("\xED\xA0\x80", 3));

  std::string const redraw = screen.render_snapshot();
  EXPECT_EQ(redraw.find("\xEF\xBF\xBD"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawPreservesIndexedBackgroundColor) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest("\x1b[48;5;42mcolored\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;42mcolored"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsTrailingColoredBackgroundCells) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 5});

  screen.ingest("\x1b[48;5;42m     \x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;42m     \x1b[0m"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsBackgroundFromEraseToEndOfLine) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 10});

  screen.ingest("\x1b[48;5;242m>\x1b[K\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m>\x1b[K\x1b[0m"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsBackgroundFromEraseWholeLine) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 10});

  screen.ingest("plain\x1b[48;5;242m\x1b[2K>\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m     >\x1b[K\x1b[0m"), std::string::npos);
  EXPECT_EQ(redraw.find("plain"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsBackgroundFromEraseBeforePromptText) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 12});

  screen.ingest(
      "\x1b[2;1H\x1b[48;5;242m\x1b[2K\x1b[0m\x1b[5G"
      "\x1b[48;5;242m>\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m    >\x1b[K\x1b[0m"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawInheritsEraseBackgroundUnderPromptText) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 12});

  screen.ingest(
      "\x1b[2;1H\x1b[48;5;242m\x1b[2K\x1b[0m\x1b[5G"
      "\x1b[38;5;11m>\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m    "), std::string::npos);
  EXPECT_NE(redraw.find("38;5;11;48;5;242m>"), std::string::npos);
  EXPECT_NE(redraw.find("48;5;242m\x1b[K"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawKeepsBackgroundFromSplitEraseToEndOfLine) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 10});

  screen.ingest("\x1b[48;5;242m>");
  screen.ingest("\x1b[");
  screen.ingest("K\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("48;5;242m>\x1b[K\x1b[0m"), std::string::npos);
}

TEST(TerminalScreenTest, DefaultEraseClearsRememberedBackgroundFill) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 10});

  screen.ingest("\x1b[48;5;242m\x1b[2K\x1b[0m");
  screen.ingest("\x1b[H\x1b[2K");

  std::string const redraw = screen.render_snapshot();
  EXPECT_EQ(redraw.find("48;5;242m\x1b[K"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawPreservesAlternateScreenMode) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest("\x1b[?1049h\x1b[48;5;42malt screen\x1b[0m");

  std::string const redraw = screen.render_snapshot();
  std::size_t const enter_alternate_screen = redraw.find("\x1b[?1049h");
  std::size_t const colored_text = redraw.find("48;5;42malt screen");

  ASSERT_NE(enter_alternate_screen, std::string::npos);
  ASSERT_NE(colored_text, std::string::npos);
  EXPECT_LT(enter_alternate_screen, colored_text);
  EXPECT_EQ(redraw.find("\x1b[?1049l"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawPreservesReverseScreenAndCursorVisibility) {
  moe::parent::TerminalScreen screen(moe::parent::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest("\x1b[?5h\x1b[?25lreverse");

  std::string const redraw = screen.render_snapshot();
  std::size_t const reverse_screen = redraw.find("\x1b[?5h");
  std::size_t const hide_cursor = redraw.find("\x1b[?25l");
  std::size_t const text = redraw.find("reverse");

  ASSERT_NE(reverse_screen, std::string::npos);
  ASSERT_NE(hide_cursor, std::string::npos);
  ASSERT_NE(text, std::string::npos);
  EXPECT_LT(reverse_screen, text);
  EXPECT_LT(hide_cursor, text);
}

}  // namespace
