#include "src/parent/status/parent_status_serializer.h"

#include <filesystem>

#include "gtest/gtest.h"
#include "src/parent/status/parent_status.h"
#include "src/parent/tray/tray_number.h"

namespace {

TEST(ParentStatusTest, SerializesAnonymousTrayStatus) {
  moe::parent::ParentStatus const status{
      .command_mode = true,
      .active_tray = moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one()),
      .overlay = moe::parent::ParentOverlayKind::WORKTREE_MANAGEMENT,
  };

  EXPECT_EQ(
      moe::parent::serialize_parent_status(status),
      R"({"type":"parent.status","commandMode":true,"trayKey":"anonymous:1","trayLabel":"tray 1","overlay":"worktreeManagement"})");
}

TEST(ParentStatusTest, EscapesWorktreeStatusStrings) {
  moe::parent::ParentStatus const status{
      .command_mode = false,
      .active_tray = moe::parent::TrayId::worktree(std::filesystem::path("/tmp/quoted\"worktree")),
      .overlay = moe::parent::ParentOverlayKind::WORKTREE_MANAGEMENT,
  };

  EXPECT_EQ(
      moe::parent::serialize_parent_status(status),
      R"({"type":"parent.status","commandMode":false,"trayKey":"worktree:/tmp/quoted\"worktree","trayLabel":"worktree /tmp/quoted\"worktree","overlay":"worktreeManagement"})");
}

}  // namespace
