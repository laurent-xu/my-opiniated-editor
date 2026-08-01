#include "src/parent/overlay/path_picker_overlay.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "src/parent/overlay/path_picker_process.h"
#include "src/parent/terminal/screen/terminal_position.h"

namespace moe::parent {
namespace {

constexpr int MAX_PICKER_ROWS = 12;
constexpr int MIN_PICKER_ROWS = 6;
constexpr std::string_view FOCUS_NOTIFICATION_PREFIX = "\x1b]697;focus;";
constexpr char FOCUS_NOTIFICATION_TERMINATOR = '\x07';

void validate_size(base::TerminalSize const size) {
  int constexpr MAX_UNSIGNED_SHORT = std::numeric_limits<unsigned short>::max();
  if (size.rows <= 0 || size.cols <= 0 || size.rows > MAX_UNSIGNED_SHORT ||
      size.cols > MAX_UNSIGNED_SHORT) {
    throw std::invalid_argument("path picker terminal size is invalid");
  }
}

}  // namespace

std::string configured_fzf_executable() {
  char const* const executable = std::getenv("MOE_FZF_EXECUTABLE");
  if (executable != nullptr && executable[0] != '\0') {
    return executable;
  }
  return "fzf";
}

std::unique_ptr<PathPickerOverlay> PathPickerOverlay::start(
    std::string fzf_executable, std::vector<std::filesystem::path> const& candidates,
    std::string prompt, base::TerminalSize const parent_size) {
  if (fzf_executable.empty()) {
    throw std::invalid_argument("fzf executable must not be empty");
  }
  base::TerminalSize const picker_size = picker_size_for(parent_size);
  std::unique_ptr<PathPickerProcess> process =
      PathPickerProcess::start(std::move(fzf_executable), std::move(prompt), picker_size);
  auto picker = std::unique_ptr<PathPickerOverlay>(
      new PathPickerOverlay(std::move(process), candidates, parent_size));
  picker->write_candidates();
  return picker;
}

PathPickerOverlay::PathPickerOverlay(std::unique_ptr<PathPickerProcess> process_value,
                                     std::vector<std::filesystem::path> candidates,
                                     base::TerminalSize const parent_size)
    : process(std::move(process_value)),
      candidate_paths(std::move(candidates)),
      parent_terminal_size(parent_size),
      picker_terminal_size(picker_size_for(parent_size)),
      terminal_screen(picker_terminal_size) {
  if (!candidate_paths.empty()) {
    highlighted_candidate_index = 0;
  }
}

PathPickerOverlay::~PathPickerOverlay() = default;

void PathPickerOverlay::write_input(std::string_view const bytes) { process->write(bytes); }

bool PathPickerOverlay::read_process_output() {
  std::optional<std::string> const output = process->read_available();
  if (!output.has_value()) {
    return false;
  }
  ingest_focus_notifications(*output);
  terminal_screen.ingest(*output);
  return true;
}

bool PathPickerOverlay::refresh_process_state() {
  if (!process->refresh_process_state()) {
    return false;
  }
  if (process->result_succeeded()) {
    read_selection();
  }
  return true;
}

void PathPickerOverlay::resize(base::TerminalSize const size) {
  parent_terminal_size = size;
  base::TerminalSize const next_picker_size = picker_size_for(size);
  if (next_picker_size.rows == picker_terminal_size.rows &&
      next_picker_size.cols == picker_terminal_size.cols) {
    return;
  }

  picker_terminal_size = next_picker_size;
  terminal_screen.resize(next_picker_size);
  process->resize(next_picker_size);
}

std::optional<base::FileDescriptor> PathPickerOverlay::process_file_descriptor() const {
  return process->file_descriptor();
}

std::string PathPickerOverlay::redraw_output() const {
  return terminal_screen.render_region_snapshot(
      TerminalPosition{.row = available_region_above().rows, .column = 0});
}

bool PathPickerOverlay::finished() const noexcept { return process->finished(); }

std::optional<std::filesystem::path> const& PathPickerOverlay::selected_path() const noexcept {
  return selection;
}

std::optional<std::size_t> PathPickerOverlay::selected_index() const noexcept {
  return selection_index;
}

std::optional<std::size_t> PathPickerOverlay::highlighted_index() const noexcept {
  return highlighted_candidate_index;
}

base::TerminalSize PathPickerOverlay::available_region_above() const noexcept {
  return {
      .rows = std::max(0, parent_terminal_size.rows - picker_terminal_size.rows),
      .cols = parent_terminal_size.cols,
  };
}

void PathPickerOverlay::write_candidates() {
  for (std::filesystem::path const& candidate : candidate_paths) {
    std::string bytes = candidate.string();
    bytes.push_back('\0');
    process->write_candidate_input(bytes);
  }
  process->close_candidate_input();
}

void PathPickerOverlay::ingest_focus_notifications(std::string_view const bytes) {
  pending_focus_notification_bytes.append(bytes);
  while (true) {
    std::size_t const start = pending_focus_notification_bytes.find(FOCUS_NOTIFICATION_PREFIX);
    if (start == std::string::npos) {
      std::size_t const retained =
          std::min(pending_focus_notification_bytes.size(), FOCUS_NOTIFICATION_PREFIX.size() - 1U);
      pending_focus_notification_bytes.erase(0, pending_focus_notification_bytes.size() - retained);
      return;
    }

    std::size_t const value_start = start + FOCUS_NOTIFICATION_PREFIX.size();
    std::size_t const end =
        pending_focus_notification_bytes.find(FOCUS_NOTIFICATION_TERMINATOR, value_start);
    if (end == std::string::npos) {
      pending_focus_notification_bytes.erase(0, start);
      return;
    }

    std::string_view const value(pending_focus_notification_bytes.data() + value_start,
                                 end - value_start);
    std::size_t index = 0;
    auto const result = std::from_chars(value.data(), value.data() + value.size(), index);
    if (result.ec == std::errc{} && result.ptr == value.data() + value.size() &&
        index < candidate_paths.size()) {
      highlighted_candidate_index = index;
    }
    pending_focus_notification_bytes.erase(0, end + 1U);
  }
}

void PathPickerOverlay::read_selection() {
  std::string const bytes = process->read_result();

  std::size_t const delimiter = bytes.find('\0');
  std::string_view const value(bytes.data(),
                               delimiter == std::string::npos ? bytes.size() : delimiter);
  std::size_t index = 0;
  auto const result = std::from_chars(value.data(), value.data() + value.size(), index);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
      index >= candidate_paths.size()) {
    return;
  }
  selection_index = index;
  selection = candidate_paths[index];
}

base::TerminalSize PathPickerOverlay::picker_size_for(base::TerminalSize const parent_size) {
  validate_size(parent_size);
  int const preferred_rows = std::max(MIN_PICKER_ROWS, parent_size.rows / 2);
  return {
      .rows = std::min({parent_size.rows, MAX_PICKER_ROWS, preferred_rows}),
      .cols = parent_size.cols,
  };
}

}  // namespace moe::parent
