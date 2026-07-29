#include "src/parent/worktree_management_overlay.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/parent/worktree_repository_registrar.h"

namespace moe::parent {
namespace {

constexpr unsigned char ESCAPE = 0x1B;
constexpr unsigned char BACKSPACE = 0x7F;
constexpr int OVERLAY_HEIGHT = 8;

std::string trimmed(std::string value) {
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ||
                            value.back() == '\n')) {
    value.pop_back();
  }
  std::size_t start = 0;
  while (start < value.size() && (value[start] == ' ' || value[start] == '\t' ||
                                  value[start] == '\r' || value[start] == '\n')) {
    ++start;
  }
  return value.substr(start);
}

void remove_last_utf8_code_point(std::string& value) {
  if (value.empty()) {
    return;
  }
  value.pop_back();
  while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0U) == 0x80U) {
    value.pop_back();
  }
}

std::size_t previous_utf8_code_point_start(std::string const& value, std::size_t const offset) {
  if (offset == 0) {
    return 0;
  }
  std::size_t previous = offset - 1;
  while (previous > 0 && (static_cast<unsigned char>(value[previous]) & 0xC0U) == 0x80U) {
    --previous;
  }
  return previous;
}

std::size_t next_utf8_code_point_end(std::string const& value, std::size_t const offset) {
  if (offset >= value.size()) {
    return value.size();
  }
  std::size_t next = offset + 1;
  while (next < value.size() && (static_cast<unsigned char>(value[next]) & 0xC0U) == 0x80U) {
    ++next;
  }
  return next;
}

std::string position_cursor(int const row, int const column) {
  return "\x1b[" + std::to_string(row) + ";" + std::to_string(column) + "H";
}

std::string visible_tail(std::string const& value, std::size_t const width) {
  if (value.size() <= width) {
    return value;
  }
  return value.substr(value.size() - width);
}

void append_filled_line(std::string& output, int const row, std::string const& text,
                        int const columns, bool const header = false) {
  std::size_t const width = static_cast<std::size_t>(std::max(columns, 1));
  std::string displayed = text.substr(0, width);
  output += position_cursor(row, 1);
  output += header ? "\x1b[7m" : "\x1b[48;5;236m\x1b[38;5;252m";
  output += displayed;
  output.append(width - displayed.size(), ' ');
  output += "\x1b[0m";
}

}  // namespace

std::unique_ptr<WorktreeManagementOverlay> WorktreeManagementOverlay::start(
    std::filesystem::path parent_executable, std::filesystem::path registry_path,
    std::filesystem::path working_directory, TerminalSize const size) {
  return std::unique_ptr<WorktreeManagementOverlay>(new WorktreeManagementOverlay(
      std::move(parent_executable), std::move(registry_path), std::move(working_directory), size));
}

WorktreeManagementOverlay::WorktreeManagementOverlay(std::filesystem::path executable,
                                                     std::filesystem::path registry,
                                                     std::filesystem::path directory,
                                                     TerminalSize const initial_size)
    : parent_executable(std::move(executable)),
      registry_path(std::move(registry)),
      working_directory(std::move(directory)),
      size(initial_size) {}

WorktreeManagementOverlay::~WorktreeManagementOverlay() = default;

void WorktreeManagementOverlay::write_input(std::string_view const bytes) {
  for (unsigned char const byte : bytes) {
    if (stage == Stage::RUNNING) {
      char const value = static_cast<char>(byte);
      process->write(std::string_view(&value, 1));
      continue;
    }
    if (stage == Stage::RESULT) {
      continue;
    }

    write_editing_input(byte);
  }
}

bool WorktreeManagementOverlay::read_process_output() {
  if (process == nullptr) {
    return false;
  }
  std::optional<std::string> const output = process->read_available();
  if (!output.has_value()) {
    return false;
  }
  append_process_output(*output);
  return true;
}

bool WorktreeManagementOverlay::refresh_process_state() {
  if (process == nullptr || stage != Stage::RUNNING) {
    return false;
  }
  std::optional<int> const exit_code = process->try_wait_for_exit();
  if (!exit_code.has_value()) {
    return false;
  }

  process = nullptr;
  append_transcript_line();
  result_succeeded = *exit_code == 0;
  stage = Stage::RESULT;
  return true;
}

void WorktreeManagementOverlay::resize(TerminalSize const next_size) {
  size = next_size;
  if (process != nullptr) {
    process->resize(next_size);
  }
}

std::optional<base::FileDescriptor> WorktreeManagementOverlay::process_file_descriptor() const {
  if (process == nullptr) {
    return std::nullopt;
  }
  return process->file_descriptor();
}

std::string WorktreeManagementOverlay::redraw_output() const {
  int const height = std::min(OVERLAY_HEIGHT, std::max(size.rows, 1));
  int const first_row = std::max(1, size.rows - height + 1);
  int const columns = std::max(size.cols, 1);
  std::vector<std::string> lines(static_cast<std::size_t>(height));
  lines[0] = " Worktrees | Add repository";
  std::size_t input_cursor_column = 1;
  auto const displayed_input = [&]() {
    std::string const prompt = "> " + input;
    std::size_t const prompt_cursor = 2 + std::min(input_cursor_offset, input.size());
    auto const width = static_cast<std::size_t>(columns);
    std::size_t const first_visible = prompt_cursor >= width ? prompt_cursor - width + 1 : 0;
    input_cursor_column = prompt_cursor - first_visible + 1;
    return prompt.substr(first_visible, width);
  };

  if (stage == Stage::REPOSITORY_ROOT) {
    if (height > 2) {
      lines[2] = error_message;
    }
    if (height > 4) {
      lines[4] = "Repository root:";
    }
    if (height > 5) {
      lines[5] = displayed_input();
    }
    if (height > 7) {
      lines[7] = "Add repository";
    }
  } else if (stage == Stage::CLONE_URL) {
    if (height > 1 && repository_root.has_value()) {
      lines[1] = visible_tail(repository_root->string(), static_cast<std::size_t>(columns));
    }
    if (height > 2) {
      lines[2] = error_message;
    }
    if (height > 4) {
      lines[4] = "Clone URL:";
    }
    if (height > 5) {
      lines[5] = displayed_input();
    }
    if (height > 7) {
      lines[7] = "Add repository";
    }
  } else {
    std::size_t const transcript_capacity = height > 3 ? static_cast<std::size_t>(height - 3) : 0;
    std::size_t const transcript_start = transcript_lines.size() > transcript_capacity
                                             ? transcript_lines.size() - transcript_capacity
                                             : 0;
    std::size_t output_row = 1;
    for (std::size_t index = transcript_start;
         index < transcript_lines.size() && output_row + 1 < static_cast<std::size_t>(height);
         ++index, ++output_row) {
      lines[output_row] = transcript_lines[index];
    }
    if (!transcript_line.empty() && output_row + 1 < static_cast<std::size_t>(height)) {
      lines[output_row] = transcript_line;
    }
    if (height > 1) {
      std::string result_line = "Working";
      if (stage == Stage::RESULT) {
        result_line = result_succeeded ? "Completed" : "Failed";
      }
      lines[static_cast<std::size_t>(height - 1)] = std::move(result_line);
    }
  }

  std::string output("\x1b[?25l");
  for (int offset = 0; offset < height; ++offset) {
    append_filled_line(output, first_row + offset, lines[static_cast<std::size_t>(offset)], columns,
                       offset == 0);
  }

  if (stage == Stage::REPOSITORY_ROOT || stage == Stage::CLONE_URL) {
    int const prompt_offset = std::min(5, height - 1);
    output += position_cursor(first_row + prompt_offset, static_cast<int>(input_cursor_column));
    output += "\x1b[?25h";
  }
  return output;
}

void WorktreeManagementOverlay::write_editing_input(unsigned char const byte) {
  if (input_sequence_state == InputSequenceState::ESCAPE) {
    input_sequence_state = byte == '[' || byte == 'O' ? InputSequenceState::CONTROL_SEQUENCE
                                                      : InputSequenceState::NORMAL;
    input_control_sequence_parameters.clear();
    return;
  }
  if (input_sequence_state == InputSequenceState::CONTROL_SEQUENCE) {
    if (byte >= 0x40U && byte <= 0x7EU) {
      handle_input_control_sequence(byte);
      input_sequence_state = InputSequenceState::NORMAL;
      input_control_sequence_parameters.clear();
    } else if (byte >= 0x20U && byte <= 0x3FU && input_control_sequence_parameters.size() < 16) {
      input_control_sequence_parameters.push_back(static_cast<char>(byte));
    } else {
      input_sequence_state = InputSequenceState::NORMAL;
      input_control_sequence_parameters.clear();
    }
    return;
  }
  if (byte == ESCAPE) {
    input_sequence_state = InputSequenceState::ESCAPE;
    return;
  }
  if (byte == '\r' || byte == '\n') {
    submit_input();
    return;
  }
  if (byte == BACKSPACE || byte == '\b') {
    erase_before_input_cursor();
    error_message.clear();
    return;
  }
  if (byte >= 0x20U) {
    input.insert(input_cursor_offset, 1, static_cast<char>(byte));
    ++input_cursor_offset;
    error_message.clear();
  }
}

void WorktreeManagementOverlay::handle_input_control_sequence(unsigned char const final_byte) {
  if (final_byte == 'D') {
    move_input_cursor_left();
  } else if (final_byte == 'C') {
    move_input_cursor_right();
  } else if (final_byte == 'H' ||
             (final_byte == '~' && (input_control_sequence_parameters == "1" ||
                                    input_control_sequence_parameters == "7"))) {
    input_cursor_offset = 0;
  } else if (final_byte == 'F' ||
             (final_byte == '~' && (input_control_sequence_parameters == "4" ||
                                    input_control_sequence_parameters == "8"))) {
    input_cursor_offset = input.size();
  } else if (final_byte == '~' && input_control_sequence_parameters == "3") {
    erase_at_input_cursor();
  }
}

void WorktreeManagementOverlay::move_input_cursor_left() {
  input_cursor_offset = previous_utf8_code_point_start(input, input_cursor_offset);
}

void WorktreeManagementOverlay::move_input_cursor_right() {
  input_cursor_offset = next_utf8_code_point_end(input, input_cursor_offset);
}

void WorktreeManagementOverlay::erase_before_input_cursor() {
  std::size_t const previous = previous_utf8_code_point_start(input, input_cursor_offset);
  input.erase(previous, input_cursor_offset - previous);
  input_cursor_offset = previous;
}

void WorktreeManagementOverlay::erase_at_input_cursor() {
  std::size_t const next = next_utf8_code_point_end(input, input_cursor_offset);
  input.erase(input_cursor_offset, next - input_cursor_offset);
}

void WorktreeManagementOverlay::clear_input() {
  input.clear();
  input_cursor_offset = 0;
  input_sequence_state = InputSequenceState::NORMAL;
  input_control_sequence_parameters.clear();
}

void WorktreeManagementOverlay::submit_input() {
  if (stage == Stage::REPOSITORY_ROOT) {
    submit_repository_root();
  } else if (stage == Stage::CLONE_URL) {
    submit_clone_url();
  }
}

void WorktreeManagementOverlay::submit_repository_root() {
  try {
    repository_root = resolved_repository_root();
    RepositoryRootState const state = inspect_repository_root(*repository_root);
    clear_input();
    error_message.clear();
    if (state == RepositoryRootState::EMPTY) {
      stage = Stage::CLONE_URL;
      return;
    }
    start_registration(std::nullopt);
  } catch (std::exception const& error) {
    error_message = error.what();
  }
}

void WorktreeManagementOverlay::submit_clone_url() {
  std::string const clone_url = trimmed(input);
  if (clone_url.empty()) {
    error_message = "Clone URL must not be empty";
    return;
  }
  clear_input();
  error_message.clear();
  try {
    start_registration(clone_url);
  } catch (std::exception const& error) {
    error_message = error.what();
  }
}

void WorktreeManagementOverlay::start_registration(std::optional<std::string> clone_url) {
  if (!repository_root.has_value()) {
    throw std::logic_error("repository root is missing");
  }

  std::vector<std::string> command{
      parent_executable.string(),
      "--register-worktree-repository",
      registry_path.string(),
      repository_root->string(),
  };
  if (clone_url.has_value()) {
    command.push_back(*clone_url);
  }

  transcript_lines.clear();
  transcript_line.clear();
  process = ContentPtySession::start(command, working_directory, size);
  stage = Stage::RUNNING;
}

void WorktreeManagementOverlay::append_process_output(std::string_view const bytes) {
  for (unsigned char const byte : bytes) {
    if (process_control_sequence) {
      if (byte >= 0x40U && byte <= 0x7EU) {
        process_control_sequence = false;
      }
      continue;
    }
    if (process_escape_sequence) {
      process_escape_sequence = false;
      if (byte == '[') {
        process_control_sequence = true;
      }
      continue;
    }
    if (byte == ESCAPE) {
      process_escape_sequence = true;
    } else if (byte == '\n' || byte == '\r') {
      append_transcript_line();
    } else if (byte == BACKSPACE || byte == '\b') {
      remove_last_utf8_code_point(transcript_line);
    } else if (byte >= 0x20U) {
      transcript_line.push_back(static_cast<char>(byte));
    }
  }
}

void WorktreeManagementOverlay::append_transcript_line() {
  if (!transcript_line.empty()) {
    transcript_lines.push_back(std::move(transcript_line));
    transcript_line.clear();
    constexpr std::size_t MAX_TRANSCRIPT_LINES = 100;
    if (transcript_lines.size() > MAX_TRANSCRIPT_LINES) {
      transcript_lines.erase(
          transcript_lines.begin(),
          transcript_lines.begin() +
              static_cast<std::ptrdiff_t>(transcript_lines.size() - MAX_TRANSCRIPT_LINES));
    }
  }
}

std::filesystem::path WorktreeManagementOverlay::resolved_repository_root() const {
  std::string value = trimmed(input);
  if (value.empty()) {
    throw std::invalid_argument("Repository root must not be empty");
  }
  if (value == "~" || value.starts_with("~/")) {
    char const* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
      throw std::runtime_error("HOME is required to expand ~");
    }
    value =
        value == "~" ? std::string(home) : (std::filesystem::path(home) / value.substr(2)).string();
  }

  std::filesystem::path path(value);
  if (path.is_relative()) {
    path = working_directory / path;
  }
  std::error_code error;
  std::filesystem::path const normalized = std::filesystem::weakly_canonical(path, error);
  if (error != std::error_code{}) {
    throw std::invalid_argument("Failed to resolve repository root: " + path.string());
  }
  return normalized;
}

}  // namespace moe::parent
