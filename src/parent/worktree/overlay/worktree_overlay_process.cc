#include "src/parent/worktree/overlay/worktree_overlay_process.h"

#include <cstddef>
#include <utility>

#include "src/parent/terminal/content_pty_session.h"

namespace moe::parent {
namespace {

constexpr unsigned char ESCAPE = 0x1B;
constexpr unsigned char BACKSPACE = 0x7F;
constexpr std::size_t MAX_TRANSCRIPT_LINES = 100;

void remove_last_utf8_code_point(std::string& value) {
  if (value.empty()) {
    return;
  }
  value.pop_back();
  while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0U) == 0x80U) {
    value.pop_back();
  }
}

}  // namespace

WorktreeOverlayProcess::WorktreeOverlayProcess() = default;

WorktreeOverlayProcess::~WorktreeOverlayProcess() = default;

void WorktreeOverlayProcess::start(std::vector<std::string> const& command,
                                   std::filesystem::path const& working_directory,
                                   base::TerminalSize const size) {
  lines.clear();
  current_line.clear();
  process = ContentPtySession::start(command, working_directory, size);
}

void WorktreeOverlayProcess::clear() {
  process = nullptr;
  lines.clear();
  current_line.clear();
  result.reset();
  escape_sequence = false;
  control_sequence = false;
}

void WorktreeOverlayProcess::write(std::string_view const bytes) const { process->write(bytes); }

bool WorktreeOverlayProcess::read_process_output() {
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

bool WorktreeOverlayProcess::refresh_process_state() {
  if (process == nullptr) {
    return false;
  }
  std::optional<process::ProcessExitStatus> const exit_status = process->try_wait_for_exit();
  if (!exit_status.has_value()) {
    return false;
  }

  process = nullptr;
  append_transcript_line();
  result = *exit_status;
  return true;
}

void WorktreeOverlayProcess::resize(base::TerminalSize const size) const {
  if (process != nullptr) {
    process->resize(size);
  }
}

std::optional<base::FileDescriptor> WorktreeOverlayProcess::file_descriptor() const {
  if (process == nullptr) {
    return std::nullopt;
  }
  return process->file_descriptor();
}

std::vector<std::string> const& WorktreeOverlayProcess::transcript_lines() const noexcept {
  return lines;
}

std::string const& WorktreeOverlayProcess::transcript_line() const noexcept { return current_line; }

bool WorktreeOverlayProcess::result_succeeded() const noexcept {
  return result.has_value() && result->succeeded();
}

void WorktreeOverlayProcess::append_process_output(std::string_view const bytes) {
  for (unsigned char const byte : bytes) {
    if (control_sequence) {
      if (byte >= 0x40U && byte <= 0x7EU) {
        control_sequence = false;
      }
      continue;
    }
    if (escape_sequence) {
      escape_sequence = false;
      if (byte == '[') {
        control_sequence = true;
      }
      continue;
    }
    if (byte == ESCAPE) {
      escape_sequence = true;
    } else if (byte == '\n' || byte == '\r') {
      append_transcript_line();
    } else if (byte == BACKSPACE || byte == '\b') {
      remove_last_utf8_code_point(current_line);
    } else if (byte >= 0x20U) {
      current_line.push_back(static_cast<char>(byte));
    }
  }
}

void WorktreeOverlayProcess::append_transcript_line() {
  if (current_line.empty()) {
    return;
  }
  lines.push_back(std::move(current_line));
  current_line.clear();
  if (lines.size() > MAX_TRANSCRIPT_LINES) {
    lines.erase(lines.begin(),
                lines.begin() + static_cast<std::ptrdiff_t>(lines.size() - MAX_TRANSCRIPT_LINES));
  }
}

}  // namespace moe::parent
