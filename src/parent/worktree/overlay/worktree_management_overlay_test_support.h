#pragma once

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

#include "src/parent/tray/tray_snapshot.h"

namespace moe::parent::test_support {

std::array<std::string_view, 3> const& worktree_overlay_mode_labels();

void save_empty_worktree_registry(std::filesystem::path const& registry_path);

std::vector<TraySnapshot> used_anonymous_tray(std::filesystem::path const& working_directory);

}  // namespace moe::parent::test_support
