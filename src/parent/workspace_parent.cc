#include "src/parent/workspace_parent.h"

#include <poll.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
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
#include "src/parent/content_pty_session.h"

namespace moe::parent {
namespace {

constexpr std::string_view DEFAULT_TERMINAL_TYPE = "xterm-256color";
constexpr TerminalSize DEFAULT_TERMINAL_SIZE{.rows = 24, .cols = 80};
constexpr int POLL_TIMEOUT_MILLISECONDS = 50;

std::sig_atomic_t volatile stop_requested = 0;

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

void forward_parent_input_to_child(ContentPtySession const& child) {
  std::array<char, 4096> buffer{};
  ssize_t const read_count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
  if (read_count <= 0) {
    if (read_count == 0 || errno == EINTR || errno == EIO) {
      return;
    }
    throw std::runtime_error(std::string("read parent input failed: ") + std::strerror(errno));
  }

  child.write(std::string_view(buffer.data(), static_cast<std::size_t>(read_count)));
}

void draw_child_output(ContentPtySession const& child) {
  std::optional<std::string> const output = child.read_available();
  if (!output.has_value()) {
    return;
  }
  write_all(base::FileDescriptor(STDOUT_FILENO), *output);
}

void synchronize_child_size_if_changed(ContentPtySession const& child, TerminalSize& last_size) {
  TerminalSize const current_size = terminal_size_from(base::FileDescriptor(STDOUT_FILENO));
  if (same_size(current_size, last_size)) {
    return;
  }
  child.resize(current_size);
  last_size = current_size;
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

  RawTerminalModeGuard const raw_terminal(base::FileDescriptor(STDIN_FILENO));
  TerminalSize last_size = terminal_size_from(base::FileDescriptor(STDOUT_FILENO));
  std::unique_ptr<ContentPtySession> child =
      ContentPtySession::start(interactive_shell_command(configured_login_shell()),
                               std::filesystem::current_path(), last_size);

  while (stop_requested == 0) {
    if (std::optional<int> const exit_code = child->try_wait_for_exit(); exit_code.has_value()) {
      return *exit_code;
    }

    synchronize_child_size_if_changed(*child, last_size);

    std::array<pollfd, 2> descriptors{
        pollfd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0},
        pollfd{.fd = child->file_descriptor().value(), .events = POLLIN, .revents = 0},
    };
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

    synchronize_child_size_if_changed(*child, last_size);

    if ((descriptors[0].revents & POLLIN) != 0) {
      forward_parent_input_to_child(*child);
    }
    if ((descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      draw_child_output(*child);
    }
  }

  return 0;
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
