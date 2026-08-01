#include "src/parent/shell/shell_configuration.h"

#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace moe::parent {

namespace {

constexpr std::string_view DEFAULT_TERMINAL_TYPE = "xterm-256color";

bool has_value(char const* const value) { return value != nullptr && value[0] != '\0'; }

}  // namespace

std::filesystem::path configured_login_shell() {
  passwd const* const user = getpwuid(getuid());
  if (user != nullptr && has_value(user->pw_shell)) {
    return user->pw_shell;
  }

  char const* const shell_from_environment = std::getenv("SHELL");
  if (has_value(shell_from_environment)) {
    return shell_from_environment;
  }

  return "/bin/sh";
}

std::vector<std::string> interactive_shell_command(std::filesystem::path const& shell_path) {
  return {shell_path.string(), "-i"};
}

std::string_view terminal_type_for_child(char const* const current_terminal_type) {
  if (!has_value(current_terminal_type) || std::strcmp(current_terminal_type, "dumb") == 0) {
    return DEFAULT_TERMINAL_TYPE;
  }
  return current_terminal_type;
}

}  // namespace moe::parent
