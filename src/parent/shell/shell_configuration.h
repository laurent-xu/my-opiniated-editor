#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace moe::parent {

std::filesystem::path configured_login_shell();
std::vector<std::string> interactive_shell_command(std::filesystem::path const& shell_path);
std::string_view terminal_type_for_child(char const* current_terminal_type);

}  // namespace moe::parent
