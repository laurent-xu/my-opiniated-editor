#include "src/parent/status/parent_status_serializer.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "src/parent/status/parent_status.h"
#include "src/parent/tray/tray_number.h"

namespace {

TEST(ParentStatusTest, SerializesAnonymousTrayStatus) {
  moe::parent::ParentStatus const status{
      .command_mode = true,
      .active_tray = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
      .overlay = moe::parent::ParentOverlayKind::WORKTREE_MANAGEMENT,
      .pane_mode = moe::parent::ParentPaneMode::MOVE_DROP,
      .pane_selected_nodes = 2,
  };

  EXPECT_EQ(
      moe::parent::serialize_parent_status(status),
      R"({"type":"parent.status","commandMode":true,"trayKey":"anonymous:1","trayLabel":"tray 1","overlay":"worktreeManagement","paneMode":"moveDrop","paneSelectedNodes":2})");
}

TEST(ParentStatusTest, EscapesWorktreeStatusStrings) {
  moe::parent::ParentStatus const status{
      .command_mode = false,
      .active_tray = moe::parent::TrayId::worktree(std::filesystem::path("/tmp/quoted\"worktree")),
      .overlay = moe::parent::ParentOverlayKind::WORKTREE_MANAGEMENT,
      .pane_mode = moe::parent::ParentPaneMode::SELECTION,
      .pane_selected_nodes = 1,
  };

  EXPECT_EQ(
      moe::parent::serialize_parent_status(status),
      R"({"type":"parent.status","commandMode":false,"trayKey":"worktree:/tmp/quoted\"worktree","trayLabel":"worktree /tmp/quoted\"worktree","overlay":"worktreeManagement","paneMode":"selection","paneSelectedNodes":1})");
}

TEST(ParentStatusTest, SerializesEveryPaneModeName) {
  struct TestCase {
    moe::parent::ParentPaneMode mode;
    std::string_view name;
  };
  constexpr std::array<TestCase, 5> CASES{{
      {.mode = moe::parent::ParentPaneMode::NONE, .name = "none"},
      {.mode = moe::parent::ParentPaneMode::SELECTION, .name = "selection"},
      {.mode = moe::parent::ParentPaneMode::MOVE_TARGET, .name = "moveTarget"},
      {.mode = moe::parent::ParentPaneMode::MOVE_DROP, .name = "moveDrop"},
      {.mode = moe::parent::ParentPaneMode::SWAP_TARGET, .name = "swapTarget"},
  }};

  for (TestCase const& test_case : CASES) {
    moe::parent::ParentStatus const status{
        .command_mode = true,
        .active_tray = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
        .overlay = moe::parent::ParentOverlayKind::NONE,
        .pane_mode = test_case.mode,
        .pane_selected_nodes = 3,
    };
    std::string const serialized = moe::parent::serialize_parent_status(status);
    EXPECT_NE(serialized.find("\"paneMode\":\"" + std::string(test_case.name) + "\""),
              std::string::npos);
  }
}

}  // namespace
