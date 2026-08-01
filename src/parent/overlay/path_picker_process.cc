#include "src/parent/overlay/path_picker_process.h"

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
#include <utility>
#include <vector>

#include "src/process/process_wait_status.h"

namespace moe::parent {
namespace {

std::runtime_error errno_error(std::string const& action) {
  return std::runtime_error(action + ": " + std::strerror(errno));
}

winsize to_winsize(base::TerminalSize const size) {
  int constexpr MAX_UNSIGNED_SHORT = std::numeric_limits<unsigned short>::max();
  if (size.rows <= 0 || size.cols <= 0 || size.rows > MAX_UNSIGNED_SHORT ||
      size.cols > MAX_UNSIGNED_SHORT) {
    throw std::invalid_argument("path picker terminal size is invalid");
  }

  winsize window_size{};
  window_size.ws_row = static_cast<unsigned short>(size.rows);
  window_size.ws_col = static_cast<unsigned short>(size.cols);
  return window_size;
}

std::vector<char*> command_argv(std::vector<std::string> const& command) {
  std::vector<char*> arguments;
  arguments.reserve(command.size() + 1);
  for (std::string const& part : command) {
    arguments.push_back(const_cast<char*>(part.c_str()));
  }
  arguments.push_back(nullptr);
  return arguments;
}

void write_all(base::FileDescriptor const descriptor, std::string_view bytes,
               std::string const& action) {
  while (!bytes.empty()) {
    ssize_t const count = ::write(descriptor.value(), bytes.data(), bytes.size());
    if (count > 0) {
      bytes.remove_prefix(static_cast<std::size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    throw errno_error(action);
  }
}

}  // namespace

std::unique_ptr<PathPickerProcess> PathPickerProcess::start(std::string fzf_executable,
                                                            std::string prompt,
                                                            base::TerminalSize const size) {
  if (fzf_executable.empty()) {
    throw std::invalid_argument("fzf executable must not be empty");
  }

  std::array<int, 2> raw_candidates{-1, -1};
  if (::pipe(raw_candidates.data()) != 0) {
    throw errno_error("create path picker candidate pipe");
  }
  base::OwnedFileDescriptor candidate_read{base::FileDescriptor(raw_candidates[0])};
  base::OwnedFileDescriptor candidate_write{base::FileDescriptor(raw_candidates[1])};

  std::array<int, 2> raw_result{-1, -1};
  if (::pipe(raw_result.data()) != 0) {
    throw errno_error("create path picker result pipe");
  }
  base::OwnedFileDescriptor result_read{base::FileDescriptor(raw_result[0])};
  base::OwnedFileDescriptor result_write{base::FileDescriptor(raw_result[1])};

  int raw_master = -1;
  winsize window_size = to_winsize(size);
  base::ProcessId const child_pid(::forkpty(&raw_master, nullptr, nullptr, &window_size));
  if (child_pid.is_error()) {
    throw errno_error("fork path picker");
  }
  if (child_pid.is_child_process()) {
    candidate_write.reset();
    result_read.reset();
    if (::dup2(candidate_read.get().value(), STDIN_FILENO) < 0 ||
        ::dup2(result_write.get().value(), STDOUT_FILENO) < 0) {
      _exit(126);
    }
    candidate_read.reset();
    result_write.reset();

    std::vector<std::string> command{
        std::move(fzf_executable),
        "--read0",
        "--print0",
        "--accept-nth={n}",
        "--no-multi",
        "--layout=reverse",
        "--border",
        "--info=inline",
        "--prompt=" + std::move(prompt),
        "--bind=focus:execute-silent(printf '\\033]697;focus;{n}\\007' > /dev/tty)",
    };
    std::vector<char*> arguments = command_argv(command);
    ::execvp(arguments[0], arguments.data());
    _exit(127);
  }

  candidate_read.reset();
  result_write.reset();
  return std::unique_ptr<PathPickerProcess>(
      new PathPickerProcess(base::OwnedFileDescriptor(base::FileDescriptor(raw_master)),
                            std::move(candidate_write), std::move(result_read), child_pid));
}

PathPickerProcess::PathPickerProcess(base::OwnedFileDescriptor terminal_master_value,
                                     base::OwnedFileDescriptor candidate_input_value,
                                     base::OwnedFileDescriptor result_output_value,
                                     base::ProcessId const child_pid)
    : terminal_master(std::move(terminal_master_value)),
      candidate_input(std::move(candidate_input_value)),
      result_output(std::move(result_output_value)),
      child_process_id(child_pid) {}

PathPickerProcess::~PathPickerProcess() { reset(); }

void PathPickerProcess::write(std::string_view const bytes) const {
  write_all(terminal_master.get(), bytes, "write path picker input");
}

void PathPickerProcess::write_candidate_input(std::string_view const bytes) const {
  write_all(candidate_input.get(), bytes, "write path picker input");
}

void PathPickerProcess::close_candidate_input() { candidate_input.reset(); }

std::optional<std::string> PathPickerProcess::read_available() const {
  std::array<char, 4096> buffer{};
  ssize_t const count = ::read(terminal_master.get().value(), buffer.data(), buffer.size());
  if (count > 0) {
    return std::string(buffer.data(), static_cast<std::size_t>(count));
  }
  if (count == 0 || errno == EIO || errno == EINTR) {
    return std::nullopt;
  }
  throw errno_error("read path picker terminal");
}

bool PathPickerProcess::refresh_process_state() {
  if (process_finished || !child_process_id.is_valid_parent_process()) {
    return false;
  }

  int status = 0;
  base::ProcessId result;
  do {
    result = base::ProcessId(::waitpid(child_process_id.value(), &status, WNOHANG));
  } while (result.is_error() && errno == EINTR);
  if (result.is_child_process()) {
    return false;
  }
  if (result != child_process_id) {
    if (result.is_error() && errno == ECHILD) {
      child_process_id = base::ProcessId{};
      process_finished = true;
      return true;
    }
    return false;
  }

  child_process_id = base::ProcessId{};
  process_finished = true;
  exit_status = process::ProcessExitStatus::from_wait_status(process::ProcessWaitStatus(status));
  return true;
}

std::string PathPickerProcess::read_result() const {
  std::string bytes;
  std::array<char, 4096> buffer{};
  while (true) {
    ssize_t const count = ::read(result_output.get().value(), buffer.data(), buffer.size());
    if (count > 0) {
      bytes.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) {
      return bytes;
    }
    if (errno != EINTR) {
      throw errno_error("read path picker result");
    }
  }
}

void PathPickerProcess::resize(base::TerminalSize const size) const {
  winsize window_size = to_winsize(size);
  if (::ioctl(terminal_master.get().value(), TIOCSWINSZ, &window_size) != 0) {
    throw errno_error("resize path picker terminal");
  }
}

std::optional<base::FileDescriptor> PathPickerProcess::file_descriptor() const {
  if (process_finished) {
    return std::nullopt;
  }
  return terminal_master.get();
}

bool PathPickerProcess::finished() const noexcept { return process_finished; }

bool PathPickerProcess::result_succeeded() const noexcept {
  return exit_status.has_value() && exit_status->succeeded();
}

void PathPickerProcess::reset() noexcept {
  terminal_master.reset();
  candidate_input.reset();
  result_output.reset();
  if (!child_process_id.is_valid_parent_process()) {
    return;
  }

  int status = 0;
  base::ProcessId result(::waitpid(child_process_id.value(), &status, WNOHANG));
  if (result.is_child_process()) {
    if (::kill(-child_process_id.value(), SIGHUP) != 0) {
      static_cast<void>(::kill(child_process_id.value(), SIGHUP));
    }
    do {
      result = base::ProcessId(::waitpid(child_process_id.value(), &status, 0));
    } while (result.is_error() && errno == EINTR);
  }
  child_process_id = base::ProcessId{};
}

}  // namespace moe::parent
