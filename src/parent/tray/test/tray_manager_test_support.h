#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "src/base/process_id.h"
#include "src/parent/tray/tray_manager.h"

namespace moe::parent::test_support {

[[nodiscard]] std::filesystem::path required_environment_path(char const* name);
[[nodiscard]] std::unique_ptr<TrayManager> start_manager();
[[nodiscard]] std::filesystem::path create_fake_worktree(std::string const& name);
[[nodiscard]] TrayNumber required_tray_number(int value);
[[nodiscard]] std::string shell_marker_command(std::string const& marker);
[[nodiscard]] std::string read_until(TrayManager& manager, std::string const& needle);
[[nodiscard]] bool process_exists(base::ProcessId process_id);
[[nodiscard]] bool wait_for_exited_tray(TrayManager& manager);

}  // namespace moe::parent::test_support
