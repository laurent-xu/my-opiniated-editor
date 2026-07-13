#include "src/bridge/parent_pty_session.h"

#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace moe::bridge {
namespace {

using base::FileDescriptor;
using base::ProcessId;

std::runtime_error errno_error(std::string const& action) {
  return std::runtime_error(action + ": " + std::strerror(errno));
}

void validate_size(PtySize const size) {
  int constexpr MAX_UNSIGNED_SHORT = std::numeric_limits<unsigned short>::max();
  if (size.rows <= 0 || size.cols <= 0 || size.rows > MAX_UNSIGNED_SHORT ||
      size.cols > MAX_UNSIGNED_SHORT) {
    throw std::invalid_argument(
        "pty size rows/cols must be within [1, " + std::to_string(MAX_UNSIGNED_SHORT) +
        "]; actual rows=" + std::to_string(size.rows) + " cols=" + std::to_string(size.cols));
  }
}

winsize to_winsize(PtySize const size) {
  validate_size(size);
  winsize window_size{};
  window_size.ws_row = static_cast<unsigned short>(size.rows);
  window_size.ws_col = static_cast<unsigned short>(size.cols);
  return window_size;
}

pid_t wait_for_child(base::ProcessId const child_pid, int const options) noexcept {
  int status = 0;
  pid_t result = -1;
  do {
    result = waitpid(child_pid.value(), &status, options);
  } while (result < 0 && errno == EINTR);
  return result;
}

void wait_for_child_exit(base::ProcessId const child_pid) noexcept {
  static_cast<void>(wait_for_child(child_pid, 0));
}

}  // namespace

std::unique_ptr<ParentPtySession> ParentPtySession::start(
    std::vector<std::string> const& command, std::filesystem::path const& working_directory,
    PtySize const size) {
  if (command.empty()) {
    throw std::invalid_argument("pty command must not be empty");
  }

  int master_fd = -1;
  winsize window_size = to_winsize(size);
  base::ProcessId const child_pid(forkpty(&master_fd, nullptr, nullptr, &window_size));
  if (child_pid.is_error()) {
    throw errno_error("forkpty failed");
  }

  if (child_pid.is_child_process()) {
    if (!working_directory.empty() && chdir(working_directory.c_str()) != 0) {
      _exit(126);
    }

    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (std::string const& part : command) {
      argv.push_back(const_cast<char*>(part.c_str()));
    }
    argv.push_back(nullptr);

    execvp(argv[0], argv.data());
    _exit(127);
  }

  return std::unique_ptr<ParentPtySession>(new ParentPtySession(
      Handles{.master_fd = base::FileDescriptor(master_fd), .child_pid = child_pid}));
}

ParentPtySession::ParentPtySession(Handles const handles)
    : master_file_descriptor(handles.master_fd), child_process_id(handles.child_pid) {}

ParentPtySession::ParentPtySession(ParentPtySession&& other) noexcept
    : master_file_descriptor(std::exchange(other.master_file_descriptor, base::FileDescriptor{})),
      child_process_id(std::exchange(other.child_process_id, base::ProcessId{})) {}

ParentPtySession& ParentPtySession::operator=(ParentPtySession&& other) noexcept {
  if (this != &other) {
    reset();
    master_file_descriptor = std::exchange(other.master_file_descriptor, base::FileDescriptor{});
    child_process_id = std::exchange(other.child_process_id, base::ProcessId{});
  }
  return *this;
}

ParentPtySession::~ParentPtySession() { reset(); }

base::ProcessId ParentPtySession::child_pid() const { return child_process_id; }

base::FileDescriptor ParentPtySession::file_descriptor() const { return master_file_descriptor; }

void ParentPtySession::write(std::string_view bytes) const {
  while (!bytes.empty()) {
    ssize_t const written = ::write(master_file_descriptor.value(), bytes.data(), bytes.size());
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw errno_error("write to pty failed");
    }
    bytes.remove_prefix(static_cast<std::size_t>(written));
  }
}

std::string ParentPtySession::read_until(std::string_view needle,
                                         std::chrono::milliseconds const timeout) const {
  std::string output;
  std::string const target(needle);
  auto const deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    pollfd descriptor{.fd = master_file_descriptor.value(), .events = POLLIN, .revents = 0};
    int const result = poll(&descriptor, 1, static_cast<int>(remaining.count()));
    if (result == 0) {
      break;
    }
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw errno_error("poll pty failed");
    }

    std::array<char, 4096> buffer{};
    ssize_t const read_count = ::read(master_file_descriptor.value(), buffer.data(), buffer.size());
    if (read_count > 0) {
      output.append(buffer.data(), static_cast<std::size_t>(read_count));
      if (output.find(target) != std::string::npos) {
        return output;
      }
      continue;
    }
    if (read_count == 0 || errno == EIO) {
      break;
    }
    if (errno != EINTR) {
      throw errno_error("read from pty failed");
    }
  }

  throw std::runtime_error("timed out waiting for '" + target + "'; output was: " + output);
}

void ParentPtySession::resize(PtySize const size) const {
  winsize window_size = to_winsize(size);
  if (ioctl(master_file_descriptor.value(), TIOCSWINSZ, &window_size) != 0) {
    throw errno_error("resize pty failed");
  }
}

void ParentPtySession::reset() noexcept {
  if (master_file_descriptor.is_valid()) {
    ::close(master_file_descriptor.value());
    master_file_descriptor = base::FileDescriptor{};
  }

  if (child_process_id.is_valid_parent_process()) {
    pid_t const result = wait_for_child(child_process_id, WNOHANG);
    if (result == 0) {
      ::kill(child_process_id.value(), SIGHUP);
      wait_for_child_exit(child_process_id);
    }
    child_process_id = base::ProcessId{};
  }
}

}  // namespace moe::bridge
