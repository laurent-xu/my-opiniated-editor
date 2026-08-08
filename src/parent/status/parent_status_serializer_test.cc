#include "src/parent/status/parent_status_serializer.h"

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "src/parent/status/parent_status.h"
#include "src/parent/tray/tray_number.h"

namespace {

moe::parent::PaneId pane_id(std::uint64_t const value) {
  std::optional<moe::parent::PaneId> const result = moe::parent::PaneId::from_value(value);
  if (!result.has_value()) {
    throw std::logic_error("test pane id must be nonzero");
  }
  return *result;
}

TEST(ParentStatusTest, SerializesAnonymousTrayStatus) {
  moe::parent::ParentStatus const status{
      .command_mode = true,
      .active_tray = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
      .overlay = moe::parent::ParentOverlayKind::WORKTREE_MANAGEMENT,
      .worktree_overlay_start_row = 12,
      .pane_mode = moe::parent::ParentPaneMode::MOVE_DROP,
      .pane_selected_nodes = 2,
  };

  EXPECT_EQ(
      moe::parent::serialize_parent_status(status),
      R"({"type":"parent.status","commandMode":true,"trayKey":"anonymous:1","trayLabel":"tray 1","overlay":"worktreeManagement","worktreeOverlayStartRow":12,"paneMode":"moveDrop","paneSelectedNodes":2})");
}

TEST(ParentStatusTest, EscapesWorktreeStatusStrings) {
  moe::parent::ParentStatus const status{
      .command_mode = false,
      .active_tray = moe::parent::TrayId::worktree(std::filesystem::path("/tmp/quoted\"worktree")),
      .overlay = moe::parent::ParentOverlayKind::WORKTREE_MANAGEMENT,
      .worktree_overlay_start_row = 16,
      .pane_mode = moe::parent::ParentPaneMode::SELECTION,
      .pane_selected_nodes = 1,
  };

  EXPECT_EQ(
      moe::parent::serialize_parent_status(status),
      R"({"type":"parent.status","commandMode":false,"trayKey":"worktree:/tmp/quoted\"worktree","trayLabel":"worktree /tmp/quoted\"worktree","overlay":"worktreeManagement","worktreeOverlayStartRow":16,"paneMode":"selection","paneSelectedNodes":1})");
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

TEST(ParentStatusTest, SerializesBrowserPaneViewAsAnNAryTree) {
  moe::parent::PaneId const pane_one = pane_id(1);
  moe::parent::PaneId const pane_two = pane_id(2);
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_one);
  static_cast<void>(layout.split_leaf(layout.root_id(), moe::parent::PaneSplitAxis::LEFT_TO_RIGHT,
                                      pane_two, moe::parent::PaneInsertion::AFTER));
  std::optional<moe::parent::PaneSelection> const no_selection;
  std::optional<moe::parent::PaneMoveSession> const no_move;
  moe::parent::ParentStatus const status{
      .command_mode = false,
      .active_tray = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
      .overlay = moe::parent::ParentOverlayKind::NONE,
      .pane_mode = moe::parent::ParentPaneMode::NONE,
      .pane_selected_nodes = 0,
  };

  std::string const serialized =
      moe::parent::serialize_parent_status(status, {.layout = layout,
                                                    .focused_pane = pane_two,
                                                    .maximized = false,
                                                    .selection = no_selection,
                                                    .move = no_move});

  EXPECT_NE(serialized.find(R"("paneView":{"focusedPane":"2","maximized":false)"),
            std::string::npos);
  EXPECT_NE(serialized.find(R"("axis":"leftToRight","percentages":[50,50])"), std::string::npos);
  EXPECT_NE(serialized.find(R"("pane":"1")"), std::string::npos);
  EXPECT_NE(serialized.find(R"("pane":"2")"), std::string::npos);
  EXPECT_NE(serialized.find(R"("selection":null,"move":null)"), std::string::npos);
}

TEST(ParentStatusTest, SerializesLivePanePreviewGeometryAndLayout) {
  moe::parent::PaneId const pane_one = pane_id(1);
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_one);
  std::optional<moe::parent::PaneSelection> const no_selection;
  std::optional<moe::parent::PaneMoveSession> const no_move;
  moe::parent::ParentStatus const status{
      .command_mode = false,
      .active_tray = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
      .overlay = moe::parent::ParentOverlayKind::WORKTREE_MANAGEMENT,
      .pane_mode = moe::parent::ParentPaneMode::NONE,
      .pane_selected_nodes = 0,
  };

  std::string const serialized =
      moe::parent::serialize_parent_status(status,
                                           {.layout = layout,
                                            .focused_pane = pane_one,
                                            .maximized = false,
                                            .selection = no_selection,
                                            .move = no_move},
                                           {.tray_key = "anonymous:2",
                                            .origin_row = 1,
                                            .origin_column = 0,
                                            .size = {.rows = 11, .cols = 100},
                                            .layout = layout,
                                            .focused_pane = pane_one,
                                            .maximized = false});

  EXPECT_NE(
      serialized.find(
          R"("panePreview":{"trayKey":"anonymous:2","origin":{"row":1,"column":0},"size":{"rows":11,"cols":100},"paneView":)"),
      std::string::npos);
  EXPECT_NE(serialized.find(R"("layout":{"id":"1","pane":"1"})"), std::string::npos);
}

}  // namespace
