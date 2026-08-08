#include "src/parent/tray/tray_manager_test_support.h"

#include <poll.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "src/parent/shell/shell_configuration.h"

namespace moe::parent::test_support {
namespace {

using namespace std::chrono_literals;

}  // namespace

std::filesystem::path required_environment_path(char const* name) {
  char const* value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error(std::string("missing environment variable: ") + name);
  }
  return {value};
}

std::unique_ptr<TrayManager> start_manager(bool const estimate_layout_sizes) {
  return TrayManager::start(TrayConfig{
      .command = interactive_shell_command(configured_login_shell()),
      .working_directory = required_environment_path("TEST_TMPDIR"),
      .initial_size = {.rows = 24, .cols = 80},
      .estimate_layout_sizes = estimate_layout_sizes,
  });
}

std::filesystem::path create_fake_worktree(std::string const& name) {
  std::filesystem::path const root = required_environment_path("TEST_TMPDIR") / name;
  std::filesystem::create_directories(root / ".git");
  return std::filesystem::weakly_canonical(root);
}

TrayNumber required_tray_number(int const value) {
  std::optional<TrayNumber> const number = TrayNumber::from_int(value);
  if (number.has_value()) {
    return *number;
  }
  throw std::runtime_error("invalid tray number in test: " + std::to_string(value));
}

std::string shell_marker_command(std::string const& marker) {
  std::ostringstream command;
  command << "printf '";
  for (unsigned char const character : marker) {
    command << '\\' << std::oct << std::setw(3) << std::setfill('0') << static_cast<int>(character);
  }
  command << "\\012'\n";
  return command.str();
}

std::string read_until(TrayManager& manager, std::string const& needle) {
  std::string output;
  auto const deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd descriptor{
        .fd = manager.active_content_file_descriptor().value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, 100);
    if (result <= 0 || (descriptor.revents & POLLIN) == 0) {
      continue;
    }

    std::optional<std::string> chunk = manager.read_active_output();
    if (!chunk.has_value()) {
      continue;
    }
    output.append(*chunk);
    if (output.find(needle) != std::string::npos) {
      return output;
    }
  }

  throw std::runtime_error("timed out waiting for '" + needle + "'; output was: " + output);
}

bool process_exists(base::ProcessId const process_id) { return ::kill(process_id.value(), 0) == 0; }

bool wait_for_exited_tray(TrayManager& manager) {
  auto const deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (manager.destroy_exited_trays()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

}  // namespace moe::parent::test_support
