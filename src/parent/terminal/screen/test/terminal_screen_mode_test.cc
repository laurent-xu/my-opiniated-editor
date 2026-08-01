#include <string>

#include "gtest/gtest.h"
#include "src/parent/terminal/screen/terminal_screen.h"

namespace {

TEST(TerminalScreenTest, RedrawPreservesAlternateScreenMode) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 20});

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
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 20});

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
