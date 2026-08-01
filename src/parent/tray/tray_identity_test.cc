#include <filesystem>
#include <optional>

#include "gtest/gtest.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/tray/tray_number.h"

namespace {

TEST(TrayNumberTest, AllowsAnonymousTraysOneThroughNine) {
  std::optional<moe::parent::TrayNumber> const tray_nine = moe::parent::TrayNumber::from_int(9);
  if (!tray_nine.has_value()) {
    FAIL() << "tray nine should be valid";
    return;
  }

  EXPECT_EQ(moe::parent::TrayNumber::one().value(), 1);
  EXPECT_EQ(tray_nine->value(), 9);
  EXPECT_FALSE(moe::parent::TrayNumber::from_int(0).has_value());
  EXPECT_FALSE(moe::parent::TrayNumber::from_int(10).has_value());
}

TEST(TrayIdTest, SupportsAnonymousAndWorktreeIdentities) {
  moe::parent::TrayId const anonymous =
      moe::parent::TrayId::anonymous(moe::parent::TrayNumber::one());
  std::filesystem::path const root = std::filesystem::path("workspace") / "tray-id-worktree";
  moe::parent::TrayId const worktree = moe::parent::TrayId::worktree(root);

  EXPECT_EQ(anonymous.kind(), moe::parent::TrayIdKind::ANONYMOUS);
  EXPECT_EQ(anonymous.key(), "anonymous:1");
  EXPECT_EQ(anonymous.label(), "tray 1");
  EXPECT_EQ(anonymous.anonymous_number().value(), 1);

  EXPECT_EQ(worktree.kind(), moe::parent::TrayIdKind::WORKTREE);
  EXPECT_EQ(worktree.worktree_root(), root);
  EXPECT_EQ(worktree.key(), "worktree:" + root.string());
  EXPECT_EQ(worktree.label(), "worktree " + root.string());
}

}  // namespace
