#include "src/parent/tray/tray_preview_renderer.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "src/base/terminal_size.h"
#include "src/parent/terminal/screen/terminal_position.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_number.h"
#include "src/parent/tray/tray_preview_request.h"

namespace {

moe::parent::TrayNumber required_tray_number(int const value) {
  std::optional<moe::parent::TrayNumber> const number = moe::parent::TrayNumber::from_int(value);
  if (!number.has_value()) {
    throw std::invalid_argument("invalid tray number");
  }
  return *number;
}

TEST(TrayPreviewRendererTest, FormatsAnonymousTrayTitle) {
  moe::parent::TrayPreviewRequest const preview{
      .tray_id = moe::parent::TrayId::anonymous(required_tray_number(7)),
      .origin = {.row = 2, .column = 3},
      .size = {.rows = 1, .cols = 24},
  };

  std::string const output = moe::parent::render_tray_preview(preview, nullptr);

  EXPECT_NE(output.find("\x1b[3;4H\x1b[48;5;236m\x1b[38;5;252m"
                        "Preview: /anonymous/7   \x1b[0m"),
            std::string::npos);
}

TEST(TrayPreviewRendererTest, FormatsWorktreeTrayTitle) {
  moe::parent::TrayPreviewRequest const preview{
      .tray_id = moe::parent::TrayId::worktree(std::filesystem::path("/repos/editor")),
      .origin = {.row = 0, .column = 0},
      .size = {.rows = 1, .cols = 24},
  };

  std::string const output = moe::parent::render_tray_preview(preview, nullptr);

  EXPECT_NE(output.find("Preview: /repos/editor  "), std::string::npos);
}

TEST(TrayPreviewRendererTest, TruncatesTitleFromTheLeft) {
  moe::parent::TrayPreviewRequest const preview{
      .tray_id = moe::parent::TrayId::worktree(std::filesystem::path("/repos/feature/branch")),
      .origin = {.row = 0, .column = 0},
      .size = {.rows = 1, .cols = 18},
  };

  std::string const output = moe::parent::render_tray_preview(preview, nullptr);

  EXPECT_NE(output.find("Preview: ...branch\x1b[0m"), std::string::npos);
  EXPECT_EQ(output.find("/repos/feature"), std::string::npos);
}

TEST(TrayPreviewRendererTest, PreservesControlStateForZeroHeightPreview) {
  moe::parent::TrayPreviewRequest const preview{
      .tray_id = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
      .origin = {.row = 4, .column = 2},
      .size = {.rows = 0, .cols = 10},
  };

  EXPECT_EQ(moe::parent::render_tray_preview(preview, nullptr),
            "\x1b[?25l\x1b[?7l\x1b[0m\x1b[?7h\x1b[?25l");
}

TEST(TrayPreviewRendererTest, OmitsHeaderForZeroWidthPreview) {
  moe::parent::TrayPreviewRequest const preview{
      .tray_id = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
      .origin = {.row = 4, .column = 2},
      .size = {.rows = 2, .cols = 0},
  };

  EXPECT_EQ(moe::parent::render_tray_preview(preview, nullptr),
            "\x1b[?25l\x1b[?7l\x1b[6;3H\x1b[0;48;5;232m"
            "\x1b[0m\x1b[?7h\x1b[?25l");
}

TEST(TrayPreviewRendererTest, RendersUnstartedTrayAsDarkBlankRegion) {
  moe::parent::TrayPreviewRequest const preview{
      .tray_id = moe::parent::TrayId::anonymous(required_tray_number(2)),
      .origin = {.row = 1, .column = 2},
      .size = {.rows = 3, .cols = 24},
  };

  std::string const output = moe::parent::render_tray_preview(preview, nullptr);

  EXPECT_NE(output.find("\x1b[3;3H\x1b[0;48;5;232m                        "), std::string::npos);
  EXPECT_NE(output.find("\x1b[4;3H\x1b[0;48;5;232m                        "), std::string::npos);
}

TEST(TrayPreviewRendererTest, ComposesContentBeforeHeader) {
  moe::parent::TrayPreviewRequest const preview{
      .tray_id = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
      .origin = {.row = 1, .column = 2},
      .size = {.rows = 2, .cols = 24},
  };

  std::string const output = moe::parent::render_tray_preview(preview, nullptr);
  std::size_t const content = output.find("\x1b[3;3H\x1b[0;48;5;232m");
  std::size_t const header = output.find("\x1b[2;3H\x1b[48;5;236m");

  ASSERT_NE(content, std::string::npos);
  ASSERT_NE(header, std::string::npos);
  EXPECT_LT(content, header);
}

}  // namespace
