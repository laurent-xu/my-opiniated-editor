#include "src/parent/worktree_picker_overlay.h"

#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/parent/terminal_position.h"

namespace moe::parent {
namespace {

constexpr int MAX_PICKER_ROWS = 12;
constexpr int MIN_PICKER_ROWS = 6;

std::runtime_error errno_error(std::string const& action) {
  return std::runtime_error(action + ": " + std::strerror(errno));
}

void validate_size(TerminalSize const size) {
  int constexpr MAX_UNSIGNED_SHORT = std::numeric_limits<unsigned short>::max();
  if (size.rows <= 0 || size.cols <= 0 || size.rows > MAX_UNSIGNED_SHORT ||
      size.cols > MAX_UNSIGNED_SHORT) {
    throw std::invalid_argument("worktree picker terminal size is invalid");
  }
}

winsize to_winsize(TerminalSize const size) {
  validate_size(size);
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

void write_all(base::FileDescriptor const descriptor, std::string_view bytes) {
  while (!bytes.empty()) {
    ssize_t const count = ::write(descriptor.value(), bytes.data(), bytes.size());
    if (count > 0) {
      bytes.remove_prefix(static_cast<std::size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    throw errno_error("write worktree picker input");
  }
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

std::string configured_fzf_executable() {
  char const* const executable = std::getenv("MOE_FZF_EXECUTABLE");
  if (executable != nullptr && executable[0] != '\0') {
    return executable;
  }
  return "fzf";
}

std::unique_ptr<WorktreePickerOverlay> WorktreePickerOverlay::start(
    std::string fzf_executable, std::vector<std::filesystem::path> const& candidates,
    TerminalSize const parent_size) {
  if (fzf_executable.empty()) {
    throw std::invalid_argument("fzf executable must not be empty");
  }
  TerminalSize const picker_size = picker_size_for(parent_size);

  std::array<int, 2> raw_candidates{-1, -1};
  if (::pipe(raw_candidates.data()) != 0) {
    throw errno_error("create worktree picker candidate pipe");
  }
  base::OwnedFileDescriptor candidate_read{base::FileDescriptor(raw_candidates[0])};
  base::OwnedFileDescriptor candidate_write{base::FileDescriptor(raw_candidates[1])};

  std::array<int, 2> raw_result{-1, -1};
  if (::pipe(raw_result.data()) != 0) {
    throw errno_error("create worktree picker result pipe");
  }
  base::OwnedFileDescriptor result_read{base::FileDescriptor(raw_result[0])};
  base::OwnedFileDescriptor result_write{base::FileDescriptor(raw_result[1])};

  int raw_master = -1;
  winsize picker_window_size = to_winsize(picker_size);
  base::ProcessId const child_pid(::forkpty(&raw_master, nullptr, nullptr, &picker_window_size));
  if (child_pid.is_error()) {
    throw errno_error("fork worktree picker");
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
        std::move(fzf_executable), "--read0",  "--print0",      "--no-multi",
        "--layout=reverse",        "--border", "--info=inline", "--prompt=Worktree> ",
    };
    std::vector<char*> arguments = command_argv(command);
    ::execvp(arguments[0], arguments.data());
    _exit(127);
  }

  candidate_read.reset();
  result_write.reset();
  auto picker = std::unique_ptr<WorktreePickerOverlay>(new WorktreePickerOverlay(
      Handles{
          .terminal_master = base::OwnedFileDescriptor(base::FileDescriptor(raw_master)),
          .candidate_input = std::move(candidate_write),
          .result_output = std::move(result_read),
          .child_pid = child_pid,
      },
      candidates, parent_size));
  picker->write_candidates();
  return picker;
}

WorktreePickerOverlay::WorktreePickerOverlay(Handles handles,
                                             std::vector<std::filesystem::path> candidates,
                                             TerminalSize const parent_size)
    : terminal_master(std::move(handles.terminal_master)),
      candidate_input(std::move(handles.candidate_input)),
      result_output(std::move(handles.result_output)),
      child_process_id(handles.child_pid),
      candidate_paths(std::move(candidates)),
      parent_terminal_size(parent_size),
      picker_terminal_size(picker_size_for(parent_size)),
      terminal_screen(picker_terminal_size) {}

WorktreePickerOverlay::~WorktreePickerOverlay() { reset_process(); }

void WorktreePickerOverlay::write_input(std::string_view const bytes) {
  write_all(terminal_master.get(), bytes);
}

bool WorktreePickerOverlay::read_process_output() {
  std::array<char, 4096> buffer{};
  ssize_t const count = ::read(terminal_master.get().value(), buffer.data(), buffer.size());
  if (count > 0) {
    terminal_screen.ingest(std::string_view(buffer.data(), static_cast<std::size_t>(count)));
    return true;
  }
  if (count == 0 || errno == EIO || errno == EINTR) {
    return false;
  }
  throw errno_error("read worktree picker terminal");
}

bool WorktreePickerOverlay::refresh_process_state() {
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
  if (result.value() != child_process_id.value()) {
    if (result.is_error() && errno == ECHILD) {
      child_process_id = base::ProcessId{};
      process_finished = true;
      return true;
    }
    return false;
  }

  child_process_id = base::ProcessId{};
  process_finished = true;
  if (exit_code_from_status(status) == 0) {
    read_selection();
  }
  return true;
}

void WorktreePickerOverlay::resize(TerminalSize const size) {
  parent_terminal_size = size;
  TerminalSize const next_picker_size = picker_size_for(size);
  if (next_picker_size.rows == picker_terminal_size.rows &&
      next_picker_size.cols == picker_terminal_size.cols) {
    return;
  }

  picker_terminal_size = next_picker_size;
  terminal_screen.resize(next_picker_size);
  winsize window_size = to_winsize(next_picker_size);
  if (::ioctl(terminal_master.get().value(), TIOCSWINSZ, &window_size) != 0) {
    throw errno_error("resize worktree picker terminal");
  }
}

std::optional<base::FileDescriptor> WorktreePickerOverlay::process_file_descriptor() const {
  if (process_finished) {
    return std::nullopt;
  }
  return terminal_master.get();
}

std::string WorktreePickerOverlay::redraw_output() const {
  int const first_row = std::max(0, parent_terminal_size.rows - picker_terminal_size.rows);
  return terminal_screen.render_region_snapshot(TerminalPosition{.row = first_row, .column = 0});
}

bool WorktreePickerOverlay::finished() const noexcept { return process_finished; }

std::optional<std::filesystem::path> const& WorktreePickerOverlay::selected_worktree()
    const noexcept {
  return selection;
}

void WorktreePickerOverlay::write_candidates() {
  for (std::filesystem::path const& candidate : candidate_paths) {
    std::string bytes = candidate.string();
    bytes.push_back('\0');
    write_all(candidate_input.get(), bytes);
  }
  candidate_input.reset();
}

void WorktreePickerOverlay::read_selection() {
  std::string bytes;
  std::array<char, 4096> buffer{};
  while (true) {
    ssize_t const count = ::read(result_output.get().value(), buffer.data(), buffer.size());
    if (count > 0) {
      bytes.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) {
      break;
    }
    if (errno != EINTR) {
      throw errno_error("read worktree picker result");
    }
  }

  std::size_t const delimiter = bytes.find('\0');
  std::filesystem::path const selected(
      bytes.substr(0, delimiter == std::string::npos ? bytes.size() : delimiter));
  if (selected.empty()) {
    return;
  }
  if (std::ranges::find(candidate_paths, selected) != candidate_paths.end()) {
    selection = selected;
  }
}

void WorktreePickerOverlay::reset_process() noexcept {
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

TerminalSize WorktreePickerOverlay::picker_size_for(TerminalSize const parent_size) {
  validate_size(parent_size);
  int const preferred_rows = std::max(MIN_PICKER_ROWS, parent_size.rows / 2);
  return {
      .rows = std::min({parent_size.rows, MAX_PICKER_ROWS, preferred_rows}),
      .cols = parent_size.cols,
  };
}

}  // namespace moe::parent
