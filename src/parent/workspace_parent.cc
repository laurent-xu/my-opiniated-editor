#include "src/parent/workspace_parent.h"

#include <istream>
#include <ostream>
#include <string>
#include <string_view>

#include "src/core/version.h"

namespace moe::parent {
namespace {

std::string trim_command(std::string_view command) {
  while (!command.empty() && (command.front() == ' ' || command.front() == '\t')) {
    command.remove_prefix(1);
  }
  while (!command.empty() &&
         (command.back() == ' ' || command.back() == '\t' || command.back() == '\r')) {
    command.remove_suffix(1);
  }
  return std::string(command);
}

bool is_quit_command(std::string_view command) { return command == "quit" || command == "exit"; }

}  // namespace

std::string startup_banner() {
  return std::string(moe::core::project_name()) + " " + std::string(moe::core::phase_name());
}

std::string prompt() { return "moe> "; }

std::string handle_command(std::string_view command) {
  std::string const trimmed = trim_command(command);
  if (trimmed.empty()) {
    return "";
  }
  if (trimmed == "help") {
    return "commands: help status quit\n";
  }
  if (trimmed == "status") {
    return "workspace-parent status=ready phase=" + std::string(moe::core::phase_name()) + "\n";
  }
  if (is_quit_command(trimmed)) {
    return "goodbye\n";
  }
  return "unknown command: " + trimmed + "\n";
}

int run_interactive(std::istream& input, std::ostream& output) {
  output << startup_banner() << '\n';
  output << "parent-ready\n";
  output << prompt();
  output.flush();

  std::string line;
  while (std::getline(input, line)) {
    std::string const trimmed = trim_command(line);
    output << handle_command(trimmed);
    if (is_quit_command(trimmed)) {
      output.flush();
      return 0;
    }
    output << prompt();
    output.flush();
  }
  return 0;
}

}  // namespace moe::parent
