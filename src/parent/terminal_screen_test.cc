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

}  // namespace
