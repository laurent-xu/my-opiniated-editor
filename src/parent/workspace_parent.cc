#include "src/parent/workspace_parent.h"

#include <poll.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/parent/tray_manager.h"
#include "src/parent/worktree_management_overlay.h"
#include "src/parent/worktree_registry_store.h"
#include "src/parent/worktree_repository_registrar.h"

namespace moe::parent {
namespace {

constexpr std::string_view DEFAULT_TERMINAL_TYPE = "xterm-256color";
constexpr TerminalSize DEFAULT_TERMINAL_SIZE{.rows = 24, .cols = 80};
constexpr int POLL_TIMEOUT_MILLISECONDS = 50;
constexpr unsigned char TRAY_COMMAND_PREFIX = 0x18;
constexpr base::FileDescriptor PARENT_INPUT_DESCRIPTOR{STDIN_FILENO};
constexpr base::FileDescriptor PARENT_OUTPUT_DESCRIPTOR{STDOUT_FILENO};

std::sig_atomic_t volatile stop_requested = 0;

enum class ParentInputMode : std::uint8_t { NORMAL, TRAY_COMMAND };

class RawTerminalModeGuard {
 public:
  explicit RawTerminalModeGuard(base::FileDescriptor const terminal) : terminal(terminal) {
    if (!terminal.is_valid() || isatty(terminal.value()) == 0) {
      return;
    }
    if (tcgetattr(terminal.value(), &original_mode) != 0) {
      throw std::runtime_error(std::string("failed to read terminal mode: ") +
                               std::strerror(errno));
    }

    termios raw_mode = original_mode;
    cfmakeraw(&raw_mode);
    if (tcsetattr(terminal.value(), TCSANOW, &raw_mode) != 0) {
      throw std::runtime_error(std::string("failed to set raw terminal mode: ") +
                               std::strerror(errno));
    }
    should_restore = true;
  }

  RawTerminalModeGuard(RawTerminalModeGuard const&) = delete;
  RawTerminalModeGuard& operator=(RawTerminalModeGuard const&) = delete;

  ~RawTerminalModeGuard() {
    if (should_restore) {
      static_cast<void>(tcsetattr(terminal.value(), TCSANOW, &original_mode));
    }
  }

 private:
  base::FileDescriptor terminal;
  termios original_mode{};
  bool should_restore = false;
};

bool has_value(char const* value) { return value != nullptr && value[0] != '\0'; }

void handle_stop_signal(int const signal_number) {
  static_cast<void>(signal_number);
  stop_requested = 1;
}

bool configure_terminal_environment() {
  std::string const terminal_type(terminal_type_for_child(std::getenv("TERM")));
  if (setenv("TERM", terminal_type.c_str(), 1) != 0) {
    std::cerr << "workspace_parent: failed to set TERM: " << std::strerror(errno) << '\n';
    return false;
  }
  return true;
}

TerminalSize sanitized_terminal_size(winsize const window_size) {
  if (window_size.ws_row == 0 || window_size.ws_col == 0) {
    return DEFAULT_TERMINAL_SIZE;
  }
  return {.rows = static_cast<int>(window_size.ws_row),
          .cols = static_cast<int>(window_size.ws_col)};
}

TerminalSize terminal_size_from(base::FileDescriptor const terminal) {
  winsize window_size{};
  if (terminal.is_valid() && ioctl(terminal.value(), TIOCGWINSZ, &window_size) == 0) {
    return sanitized_terminal_size(window_size);
  }
  return DEFAULT_TERMINAL_SIZE;
}

bool same_size(TerminalSize const left, TerminalSize const right) {
  return left.rows == right.rows && left.cols == right.cols;
}

pollfd readable_descriptor(base::FileDescriptor const descriptor) {
  return {.fd = descriptor.value(), .events = POLLIN, .revents = 0};
}

void write_all(base::FileDescriptor const output, std::string_view bytes) {
  while (!bytes.empty()) {
    ssize_t const written = ::write(output.value(), bytes.data(), bytes.size());
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
    }
    bytes.remove_prefix(static_cast<std::size_t>(written));
  }
}

void write_active_surface_input(TrayManager& trays, std::string_view const bytes) {
  WorktreeManagementOverlay* const overlay = trays.active_worktree_management_overlay();
  if (overlay != nullptr) {
    overlay->write_input(bytes);
    return;
  }
  trays.write_input(bytes);
}

void write_active_surface_input_byte(TrayManager& trays, unsigned char const byte) {
  char const value = static_cast<char>(byte);
  write_active_surface_input(trays, std::string_view(&value, 1));
}

void redraw_active_surface(TrayManager const& trays) {
  write_all(PARENT_OUTPUT_DESCRIPTOR, trays.active_redraw_output());
  WorktreeManagementOverlay const* const overlay = trays.active_worktree_management_overlay();
  if (overlay != nullptr) {
    write_all(PARENT_OUTPUT_DESCRIPTOR, overlay->redraw_output());
  }
}

bool is_anonymous_tray_command(unsigned char const byte) { return byte >= '1' && byte <= '9'; }

std::filesystem::path current_parent_executable() {
  std::error_code error;
  std::filesystem::path const executable = std::filesystem::read_symlink("/proc/self/exe", error);
  if (error != std::error_code{}) {
    throw std::filesystem::filesystem_error("resolve workspace parent executable", "/proc/self/exe",
                                            error);
  }
  return executable;
}

void toggle_worktree_management_overlay(TrayManager& trays,
                                        std::filesystem::path const& parent_executable,
                                        TerminalSize const size) {
  if (trays.active_worktree_management_overlay() != nullptr) {
    trays.clear_active_worktree_management_overlay();
    redraw_active_surface(trays);
    return;
  }

  std::filesystem::path const working_directory = trays.active_snapshot().working_directory;
  trays.set_active_worktree_management_overlay(WorktreeManagementOverlay::start(
      parent_executable, WorktreeRegistryStore::default_registry_path(), working_directory, size));
  redraw_active_surface(trays);
}

void handle_tray_command_byte(TrayManager& trays, unsigned char const byte,
                              std::filesystem::path const& parent_executable,
                              TerminalSize const size) {
  if (is_anonymous_tray_command(byte)) {
    std::optional<TrayNumber> const number = TrayNumber::from_int(static_cast<int>(byte - '0'));
    if (number.has_value()) {
      static_cast<void>(trays.switch_to(*number));
      redraw_active_surface(trays);
      return;
    }
  }
  if (byte == 'w') {
    toggle_worktree_management_overlay(trays, parent_executable, size);
    return;
  }

  write_active_surface_input_byte(trays, TRAY_COMMAND_PREFIX);
  write_active_surface_input_byte(trays, byte);
}

void route_parent_input_to_active_tray(TrayManager& trays, std::string_view const bytes,
                                       ParentInputMode& input_mode,
                                       std::filesystem::path const& parent_executable,
                                       TerminalSize const size) {
  std::string forwarded;
  forwarded.reserve(bytes.size());

  auto flush_forwarded = [&]() {
    if (!forwarded.empty()) {
      write_active_surface_input(trays, forwarded);
      forwarded.clear();
    }
  };

  for (unsigned char const byte : bytes) {
    if (input_mode == ParentInputMode::NORMAL) {
      if (byte == TRAY_COMMAND_PREFIX) {
        flush_forwarded();
        input_mode = ParentInputMode::TRAY_COMMAND;
        continue;
      }
      forwarded.push_back(static_cast<char>(byte));
      continue;
    }

    handle_tray_command_byte(trays, byte, parent_executable, size);
    input_mode = ParentInputMode::NORMAL;
  }

  flush_forwarded();
  WorktreeManagementOverlay const* const overlay = trays.active_worktree_management_overlay();
  if (overlay != nullptr) {
    write_all(PARENT_OUTPUT_DESCRIPTOR, overlay->redraw_output());
  }
}

void forward_parent_input_to_active_tray(TrayManager& trays, ParentInputMode& input_mode,
                                         std::filesystem::path const& parent_executable,
                                         TerminalSize const size) {
  std::array<char, 4096> buffer{};
  ssize_t const read_count = ::read(PARENT_INPUT_DESCRIPTOR.value(), buffer.data(), buffer.size());
  if (read_count <= 0) {
    if (read_count == 0 || errno == EINTR || errno == EIO) {
      return;
    }
    throw std::runtime_error(std::string("read parent input failed: ") + std::strerror(errno));
  }

  route_parent_input_to_active_tray(
      trays, std::string_view(buffer.data(), static_cast<std::size_t>(read_count)), input_mode,
      parent_executable, size);
}

void draw_tray_output(TrayManager& trays, TrayOutputSource const& source) {
  std::optional<std::string> const output = trays.read_output(source.tray_id);
  if (!output.has_value()) {
    return;
  }
  if (source.tray_id == trays.active_id() &&
      trays.active_worktree_management_overlay() == nullptr) {
    write_all(PARENT_OUTPUT_DESCRIPTOR, *output);
  }
}

void synchronize_active_tray_size_if_changed(TrayManager& trays, TerminalSize& last_size) {
  TerminalSize const current_size = terminal_size_from(PARENT_OUTPUT_DESCRIPTOR);
  if (same_size(current_size, last_size)) {
    return;
  }
  trays.resize_active(current_size);
  redraw_active_surface(trays);
  last_size = current_size;
}

int run_repository_registration_helper(std::span<char*> const arguments) {
  if (arguments.size() != 4 && arguments.size() != 5) {
    std::cerr << "usage: workspace_parent --register-worktree-repository "
                 "<registry-path> <repository-root> [clone-url]\n";
    return 2;
  }

  RepositoryRegistrationRequest request{
      .repository_root = arguments[3],
      .clone_url = std::nullopt,
      .registry_path = arguments[2],
  };
  if (arguments.size() == 5) {
    request.clone_url = arguments[4];
  }

  try {
    WorktreeRepositoryRegistrar(configured_git_executable())
        .register_repository(request, std::cout);
    return 0;
  } catch (std::exception const& error) {
    std::cerr << "Repository registration failed: " << error.what() << '\n';
    return 1;
  }
}

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

int run_workspace_parent() {
  stop_requested = 0;
  std::signal(SIGTERM, handle_stop_signal);
  std::signal(SIGINT, handle_stop_signal);

  if (!configure_terminal_environment()) {
    return 126;
  }

  RawTerminalModeGuard const raw_terminal(PARENT_INPUT_DESCRIPTOR);
  TerminalSize last_size = terminal_size_from(PARENT_OUTPUT_DESCRIPTOR);
  ParentInputMode input_mode = ParentInputMode::NORMAL;
  std::filesystem::path const working_directory = std::filesystem::current_path();
  std::filesystem::path const parent_executable = current_parent_executable();
  std::unique_ptr<TrayManager> trays = TrayManager::start(TrayConfig{
      .command = interactive_shell_command(configured_login_shell()),
      .working_directory = working_directory,
      .initial_size = last_size,
  });

  while (stop_requested == 0) {
    if (std::optional<int> const exit_code = trays->try_wait_for_active_exit();
        exit_code.has_value()) {
      return *exit_code;
    }

    synchronize_active_tray_size_if_changed(*trays, last_size);

    std::vector<TrayOutputSource> overlay_sources =
        trays->worktree_management_overlay_output_sources();
    std::vector<TrayOutputSource> output_sources = trays->output_sources();
    std::vector<pollfd> descriptors;
    descriptors.reserve(overlay_sources.size() + output_sources.size() + 1);
    descriptors.push_back(readable_descriptor(PARENT_INPUT_DESCRIPTOR));
    std::size_t const overlay_descriptor_start = descriptors.size();
    for (TrayOutputSource const& source : overlay_sources) {
      descriptors.push_back(readable_descriptor(source.file_descriptor));
    }
    std::size_t const tray_descriptor_start = descriptors.size();
    for (TrayOutputSource const& source : output_sources) {
      descriptors.push_back(readable_descriptor(source.file_descriptor));
    }

    int const result = poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()),
                            POLL_TIMEOUT_MILLISECONDS);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("poll workspace parent failed: ") +
                               std::strerror(errno));
    }
    if (result == 0) {
      continue;
    }

    synchronize_active_tray_size_if_changed(*trays, last_size);

    if ((descriptors[0].revents & POLLIN) != 0) {
      forward_parent_input_to_active_tray(*trays, input_mode, parent_executable, last_size);
    }

    for (std::size_t index = 0; index < overlay_sources.size(); ++index) {
      WorktreeManagementOverlay* const overlay =
          trays->worktree_management_overlay(overlay_sources[index].tray_id);
      if (overlay == nullptr) {
        continue;
      }

      pollfd const& descriptor = descriptors[overlay_descriptor_start + index];
      bool changed = false;
      if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        changed = overlay->read_process_output();
      }
      changed = overlay->refresh_process_state() || changed;
      if (changed && overlay_sources[index].tray_id == trays->active_id()) {
        write_all(PARENT_OUTPUT_DESCRIPTOR, overlay->redraw_output());
      }
    }

    for (std::size_t index = 0; index < output_sources.size(); ++index) {
      pollfd const& descriptor = descriptors[tray_descriptor_start + index];
      if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        draw_tray_output(*trays, output_sources[index]);
      }
    }
  }

  return 0;
}

int run_workspace_parent_command(std::span<char*> const arguments) {
  if (arguments.size() >= 2 && std::string_view(arguments[1]) == "--register-worktree-repository") {
    return run_repository_registration_helper(arguments);
  }
  if (arguments.size() != 1) {
    std::cerr << "usage: workspace_parent\n";
    return 2;
  }
  return run_workspace_parent();
}

int exec_configured_login_shell() {
  if (!configure_terminal_environment()) {
    return 126;
  }

  std::vector<std::string> const command = interactive_shell_command(configured_login_shell());

  std::vector<char*> argv;
  argv.reserve(command.size() + 1);
  for (std::string const& part : command) {
    argv.push_back(const_cast<char*>(part.c_str()));
  }
  argv.push_back(nullptr);

  execvp(argv[0], argv.data());
  std::cerr << "workspace_parent: failed to exec " << command.front() << ": "
            << std::strerror(errno) << '\n';
  return 127;
}

}  // namespace moe::parent
