#pragma once

#include <span>

namespace moe::parent {

int run_workspace_parent();
int run_workspace_parent_command(std::span<char*> arguments);
int exec_configured_login_shell();

}  // namespace moe::parent
