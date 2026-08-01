#include <string>

#include "gtest/gtest.h"
#include "src/base/terminal_size.h"
#include "src/parent/terminal/screen/terminal_position.h"
#include "src/parent/terminal/screen/test/terminal_screen_snapshot_test_support.h"

namespace {

using moe::base::TerminalSize;
using moe::parent::TerminalPosition;
using moe::parent::test_support::expected_full_snapshot;
using moe::parent::test_support::render_snapshot;

TEST(TerminalScreenStateSnapshotTest, EnteringAlternateScreenClearsRememberedFill) {
  std::string const actual = render_snapshot(TerminalSize{.rows = 1, .cols = 4},
                                             {"\x1b[48;5;42m\x1b[2K\x1b[0m\x1b[?1049h"});
  EXPECT_EQ(actual, expected_full_snapshot("", TerminalPosition{}, true));
}

TEST(TerminalScreenStateSnapshotTest, CharacterizesFillMovementDuringScroll) {
  std::string const actual =
      render_snapshot(TerminalSize{.rows = 4, .cols = 8},
                      {"\x1b[3;1H\x1b[48;5;242mx\x1b[K\x1b[0m", "\x1b[2;4r\x1b[4;1H\n\x1b[r"});
  std::string const body = "\x1b[2;1H\x1b[0;39;48;5;242mx\x1b[K\x1b[0m";
  EXPECT_EQ(actual, expected_full_snapshot(body, TerminalPosition{}));
}

TEST(TerminalScreenStateSnapshotTest, CharacterizesExplicitStyleInScrollback) {
  std::string const actual = render_snapshot(TerminalSize{.rows = 2, .cols = 4},
                                             {"\x1b[48;5;42mabc\x1b[0m\r\nnext\r\nlast"});
  std::string const body =
      "\x1b[0;39;48;5;42mabc\x1b[0m\r\n\r\n\r\n"
      "\x1b[0m\x1b[H\x1b[2J\x1b[1;1Hnext\x1b[2;1Hlast";
  EXPECT_EQ(actual, expected_full_snapshot(body, TerminalPosition{.row = 1, .column = 3}));
}

TEST(TerminalScreenStateSnapshotTest, CharacterizesEraseFillWhenLineEntersScrollback) {
  std::string const actual = render_snapshot(TerminalSize{.rows = 2, .cols = 4},
                                             {"\x1b[48;5;42mx\x1b[K\x1b[0m\r\nnext\r\nlast"});
  std::string const body =
      "\x1b[0;39;48;5;42mx\x1b[K\x1b[0m\r\n\r\n\r\n"
      "\x1b[0m\x1b[H\x1b[2J\x1b[1;1Hnext\x1b[2;1Hlast";
  EXPECT_EQ(actual, expected_full_snapshot(body, TerminalPosition{.row = 1, .column = 3}));
}

}  // namespace
