#include "src/parent/worktree/overlay/worktree_management_overlay_test_support.h"

#include "src/base/process_id.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_number.h"
#include "src/parent/worktree/worktree_registry_store.h"

namespace moe::parent::test_support {
namespace {

constexpr std::array<std::string_view, 3> MODE_LABELS{
    "Worktrees",
    "Add worktree",
    "Add repository",
};

}  // namespace

std::array<std::string_view, 3> const& worktree_overlay_mode_labels() { return MODE_LABELS; }

void save_empty_worktree_registry(std::filesystem::path const& registry_path) {
  WorktreeRegistryStore(registry_path).save(WorktreeRegistryStore::empty_registry());
}

std::vector<TraySnapshot> used_anonymous_tray(std::filesystem::path const& working_directory) {
  return {
      TraySnapshot{
          .id = TrayId::anonymous(TrayNumber::one()),
          .label = "tray 1",
          .working_directory = working_directory,
          .child_pid = base::ProcessId{},
      },
  };
}

}  // namespace moe::parent::test_support
