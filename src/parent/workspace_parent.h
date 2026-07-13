#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace moe::parent {

std::filesystem::path configured_login_shell();
std::vector<std::string> interactive_shell_command(std::filesystem::path const& shell_path);
int exec_configured_login_shell();

}  // namespace moe::parent
