#include <string>

#include "gtest/gtest.h"
#include "src/parent/terminal/terminal_screen.h"

namespace {

TEST(TerminalScreenTest, RedrawPreservesUtf8SplitAcrossInputChunks) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest(std::string("\xE2\x94", 2));
  screen.ingest(std::string("\x80", 1));

  std::string const redraw = screen.render_snapshot();
  EXPECT_NE(redraw.find("\xE2\x94\x80"), std::string::npos);
  EXPECT_EQ(redraw.find("\xEF\xBF\xBD"), std::string::npos);
}

TEST(TerminalScreenTest, RedrawDoesNotEmitReplacementGlyphForInvalidUtf8) {
  moe::parent::TerminalScreen screen(moe::base::TerminalSize{.rows = 3, .cols = 20});

  screen.ingest(std::string("\xED\xA0\x80", 3));

  std::string const redraw = screen.render_snapshot();
  EXPECT_EQ(redraw.find("\xEF\xBF\xBD"), std::string::npos);
}

}  // namespace
