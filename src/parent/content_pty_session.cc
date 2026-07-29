#include "src/parent/content_pty_session.h"

#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace moe::parent {
namespace {

std::runtime_error errno_error(std::string const& action) {
  return std::runtime_error(action + ": " + std::strerror(errno));
}

void validate_size(TerminalSize const size) {
  int constexpr MAX_UNSIGNED_SHORT = std::numeric_limits<unsigned short>::max();
  if (size.rows <= 0 || size.cols <= 0 || size.rows > MAX_UNSIGNED_SHORT ||
      size.cols > MAX_UNSIGNED_SHORT) {
    throw std::invalid_argument(
        "terminal size rows/cols must be within [1, " + std::to_string(MAX_UNSIGNED_SHORT) +
        "]; actual rows=" + std::to_string(size.rows) + " cols=" + std::to_string(size.cols));
  }
}

winsize to_winsize(TerminalSize const size) {
  validate_size(size);
  winsize window_size{};
  window_size.ws_row = static_cast<unsigned short>(size.rows);
  window_size.ws_col = static_cast<unsigned short>(size.cols);
  return window_size;
}

base::ProcessId wait_for_child(base::ProcessId const child_pid, int const options,
                               int& status) noexcept {
  base::ProcessId result;
  do {
    result = base::ProcessId(waitpid(child_pid.value(), &status, options));
  } while (result.is_error() && errno == EINTR);
  return result;
}

int exit_code_from_status(int const status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 1;
}

}  // namespace

std::unique_ptr<ContentPtySession> ContentPtySession::start(
    std::vector<std::string> const& command, std::filesystem::path const& working_directory,
    TerminalSize const size) {
  if (command.empty()) {
    throw std::invalid_argument("content pty command must not be empty");
  }

  int raw_master_fd = -1;
  winsize window_size = to_winsize(size);
  base::ProcessId const child_pid(forkpty(&raw_master_fd, nullptr, nullptr, &window_size));
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

  return std::unique_ptr<ContentPtySession>(new ContentPtySession(
      Handles{.master_fd = base::OwnedFileDescriptor(base::FileDescriptor(raw_master_fd)),
              .child_pid = child_pid}));
}

ContentPtySession::ContentPtySession(Handles handles)
    : master_file_descriptor(std::move(handles.master_fd)), child_process_id(handles.child_pid) {}

ContentPtySession::ContentPtySession(ContentPtySession&& other) noexcept
    : master_file_descriptor(std::move(other.master_file_descriptor)),
      child_process_id(std::exchange(other.child_process_id, base::ProcessId{})) {}

ContentPtySession& ContentPtySession::operator=(ContentPtySession&& other) noexcept {
  if (this != &other) {
    reset();
    master_file_descriptor = std::move(other.master_file_descriptor);
    child_process_id = std::exchange(other.child_process_id, base::ProcessId{});
  }
  return *this;
}

ContentPtySession::~ContentPtySession() { reset(); }

base::ProcessId ContentPtySession::child_pid() const { return child_process_id; }

base::FileDescriptor ContentPtySession::file_descriptor() const {
  return master_file_descriptor.get();
}

void ContentPtySession::write(std::string_view bytes) const {
  while (!bytes.empty()) {
    ssize_t const written =
        ::write(master_file_descriptor.get().value(), bytes.data(), bytes.size());
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw errno_error("write to content pty failed");
    }
    bytes.remove_prefix(static_cast<std::size_t>(written));
  }
}

std::optional<std::string> ContentPtySession::read_available() const {
  std::array<char, 4096> buffer{};
  ssize_t const read_count =
      ::read(master_file_descriptor.get().value(), buffer.data(), buffer.size());
  if (read_count > 0) {
    return std::string(buffer.data(), static_cast<std::size_t>(read_count));
  }
  if (read_count == 0 || errno == EIO || errno == EINTR) {
    return std::nullopt;
  }
  throw errno_error("read from content pty failed");
}

void ContentPtySession::resize(TerminalSize const size) const {
  winsize window_size = to_winsize(size);
  if (ioctl(master_file_descriptor.get().value(), TIOCSWINSZ, &window_size) != 0) {
    throw errno_error("resize content pty failed");
  }
}

std::optional<int> ContentPtySession::try_wait_for_exit() noexcept {
  if (!child_process_id.is_valid_parent_process()) {
    return 0;
  }

  int status = 0;
  base::ProcessId const result = wait_for_child(child_process_id, WNOHANG, status);
  if (result.is_child_process()) {
    return std::nullopt;
  }
  if (result.value() == child_process_id.value()) {
    child_process_id = base::ProcessId{};
    return exit_code_from_status(status);
  }
  if (result.is_error() && errno == ECHILD) {
    child_process_id = base::ProcessId{};
    return 0;
  }
  return std::nullopt;
}

void ContentPtySession::reset() noexcept {
  master_file_descriptor.reset();

  if (child_process_id.is_valid_parent_process()) {
    int status = 0;
    base::ProcessId const result = wait_for_child(child_process_id, WNOHANG, status);
    if (result.is_child_process()) {
      if (::kill(-child_process_id.value(), SIGHUP) != 0) {
        static_cast<void>(::kill(child_process_id.value(), SIGHUP));
      }
      static_cast<void>(wait_for_child(child_process_id, 0, status));
    }
    child_process_id = base::ProcessId{};
  }
}

}  // namespace moe::parent
