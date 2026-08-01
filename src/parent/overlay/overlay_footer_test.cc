#include "src/parent/overlay/overlay_footer.h"

#include <array>
#include <string_view>

#include "gtest/gtest.h"

namespace {

TEST(OverlayFooterTest, HighlightsSelectionWithoutChangingLabelText) {
  constexpr std::array<std::string_view, 2> LABELS{"One", "Two"};

  EXPECT_EQ(moe::parent::render_overlay_footer(LABELS, 1, {.rows = 3, .cols = 12}),
            "\x1b[3;1H\x1b[48;5;236m\x1b[38;5;252mOne  \x1b[48;5;244mTwo"
            "\x1b[48;5;236m    \x1b[0m");
}

}  // namespace
