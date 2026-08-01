#include "src/parent/worktree_management_overlay.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/parent/overlay_footer.h"
#include "src/parent/path_picker_overlay.h"
#include "src/parent/terminal/content_pty_session.h"
#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/tray_action_kind.h"
#include "src/parent/worktree/repository_root_state.h"
#include "src/parent/worktree/worktree_candidate_finder.h"
#include "src/parent/worktree/worktree_provisioner.h"
#include "src/parent/worktree/worktree_registry_store.h"
#include "src/parent/worktree/worktree_repository_registrar.h"
#include "src/process/process_exit_status.h"

namespace moe::parent {

using base::TerminalSize;

namespace {

constexpr unsigned char ESCAPE = 0x1B;
constexpr unsigned char BACKSPACE = 0x7F;
constexpr int DIALOG_HEIGHT = 7;
constexpr std::array<std::string_view, 3> MODE_LABELS{
    "Worktrees",
    "Add worktree",
    "Add repository",
};

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
  std::size_t previous = offset - 1U;
  while (previous > 0 && (static_cast<unsigned char>(value[previous]) & 0xC0U) == 0x80U) {
    --previous;
  }
  return previous;
}

std::size_t next_utf8_code_point_end(std::string const& value, std::size_t const offset) {
  if (offset >= value.size()) {
    return value.size();
  }
  std::size_t next = offset + 1U;
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
                        int const columns) {
  std::size_t const width = static_cast<std::size_t>(std::max(columns, 1));
  std::string displayed = text.substr(0, width);
  output += position_cursor(row, 1);
  output += "\x1b[48;5;236m\x1b[38;5;252m";
  output += displayed;
  output.append(width - displayed.size(), ' ');
  output += "\x1b[0m";
}

}  // namespace

std::unique_ptr<WorktreeManagementOverlay> WorktreeManagementOverlay::start(
    std::filesystem::path parent_executable, std::filesystem::path registry_path,
    std::filesystem::path working_directory, std::string git_executable, std::string fzf_executable,
    std::vector<TraySnapshot> const& session_trays, TerminalSize const size) {
  return std::unique_ptr<WorktreeManagementOverlay>(new WorktreeManagementOverlay(
      std::move(parent_executable), std::move(registry_path), std::move(working_directory),
      std::move(git_executable), std::move(fzf_executable), session_trays, size));
}

WorktreeManagementOverlay::WorktreeManagementOverlay(std::filesystem::path executable,
                                                     std::filesystem::path registry,
                                                     std::filesystem::path directory,
                                                     std::string git, std::string fzf,
                                                     std::vector<TraySnapshot> const& session_trays,
                                                     TerminalSize const initial_size)
    : parent_executable(std::move(executable)),
      registry_path(std::move(registry)),
      working_directory(std::move(directory)),
      git_executable(std::move(git)),
      fzf_executable(std::move(fzf)),
      size(initial_size) {
  update_session_trays(session_trays);
  load_repositories();
  activate_mode();
}

WorktreeManagementOverlay::~WorktreeManagementOverlay() = default;

void WorktreeManagementOverlay::write_input(std::string_view const bytes) {
  if (current_stage() == Stage::RUNNING) {
    process->write(bytes);
    return;
  }
  for (unsigned char const byte : bytes) {
    write_mode_switch_input(byte);
  }
}

bool WorktreeManagementOverlay::read_process_output() {
  if (picker != nullptr) {
    return picker->read_process_output();
  }
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
  if (picker != nullptr) {
    bool const changed = picker->refresh_process_state();
    if (!picker->finished()) {
      return changed;
    }

    std::optional<std::filesystem::path> const selected = picker->selected_path();
    std::optional<std::size_t> const selected_index = picker->selected_index();
    picker = nullptr;
    full_redraw_requested = true;
    if (!selected.has_value()) {
      active_error_message() = "No path selected";
      return true;
    }
    if (mode == Mode::SWITCH_WORKTREE) {
      if (!selected_index.has_value() || *selected_index >= switch_candidate_tray_ids.size() ||
          *selected_index >= switch_candidate_available.size()) {
        switch_worktree_error_message = "Selected tray is unavailable";
        return true;
      }
      if (!switch_candidate_available[*selected_index]) {
        start_switch_worktree_picker();
        set_picker_action_error("Worktree is unavailable; use Shift+R to remove it");
        return true;
      }
      tray_to_open = switch_candidate_tray_ids[*selected_index];
      switch_worktree_error_message.clear();
    } else if (mode == Mode::ADD_WORKTREE) {
      selected_repository = *selected;
      worktree_error_message.clear();
      worktree_stage = Stage::WORKTREE_BRANCH;
    }
    return true;
  }
  if (process == nullptr || current_stage() != Stage::RUNNING) {
    return false;
  }
  std::optional<process::ProcessExitStatus> const exit_status = process->try_wait_for_exit();
  if (!exit_status.has_value()) {
    return false;
  }

  process = nullptr;
  append_transcript_line();
  result_succeeded = exit_status->succeeded();
  mutable_current_stage() = Stage::RESULT;
  if (result_succeeded && mode == Mode::ADD_WORKTREE) {
    if (!pending_worktree_path.has_value()) {
      throw std::logic_error("successful worktree provision is missing its path");
    }
    tray_to_open = TrayId::worktree(pending_worktree_path.value());
  }
  return true;
}

void WorktreeManagementOverlay::resize(TerminalSize const next_size) {
  size = next_size;
  if (process != nullptr) {
    process->resize(next_size);
  }
  if (picker != nullptr) {
    picker->resize(dialog_terminal_size());
  }
}

std::optional<base::FileDescriptor> WorktreeManagementOverlay::process_file_descriptor() const {
  if (picker != nullptr) {
    return picker->process_file_descriptor();
  }
  if (process == nullptr) {
    return std::nullopt;
  }
  return process->file_descriptor();
}

bool WorktreeManagementOverlay::take_full_redraw_request() noexcept {
  return std::exchange(full_redraw_requested, false);
}

std::optional<TrayId> WorktreeManagementOverlay::take_tray_to_open() {
  return std::exchange(tray_to_open, std::nullopt);
}

std::optional<TrayPreviewRequest> WorktreeManagementOverlay::preview_request() const {
  if (mode != Mode::SWITCH_WORKTREE || picker == nullptr) {
    return std::nullopt;
  }
  std::optional<std::size_t> const index = picker->highlighted_index();
  TerminalSize const available_region = picker->available_region_above();
  if (!index.has_value() || *index >= switch_candidate_tray_ids.size() ||
      available_region.rows <= 0) {
    return std::nullopt;
  }
  return TrayPreviewRequest{
      .tray_id = switch_candidate_tray_ids[*index],
      .origin = TerminalPosition{.row = 0, .column = 0},
      .size = available_region,
  };
}

std::optional<TrayId> WorktreeManagementOverlay::highlighted_tray_id() const {
  if (mode != Mode::SWITCH_WORKTREE || picker == nullptr) {
    return std::nullopt;
  }
  std::optional<std::size_t> const index = picker->highlighted_index();
  if (!index.has_value() || *index >= switch_candidate_tray_ids.size()) {
    return std::nullopt;
  }
  return switch_candidate_tray_ids[*index];
}

bool WorktreeManagementOverlay::begin_tray_action_confirmation(TrayActionKind const kind) {
  std::optional<TrayId> const highlighted = highlighted_tray_id();
  if (!highlighted.has_value()) {
    return false;
  }
  tray_action_confirmation = TrayActionRequest{.kind = kind, .tray_id = *highlighted};
  picker_action_error.clear();
  full_redraw_requested = true;
  return true;
}

bool WorktreeManagementOverlay::has_tray_action_confirmation() const noexcept {
  return tray_action_confirmation.has_value();
}

std::optional<TrayActionRequest> WorktreeManagementOverlay::resolve_tray_action_confirmation(
    bool const confirmed) {
  if (!tray_action_confirmation.has_value()) {
    return std::nullopt;
  }
  std::optional<TrayActionRequest> request = std::exchange(tray_action_confirmation, std::nullopt);
  full_redraw_requested = true;
  return confirmed ? request : std::nullopt;
}

void WorktreeManagementOverlay::cancel_tray_action_confirmation() {
  tray_action_confirmation.reset();
  full_redraw_requested = true;
}

void WorktreeManagementOverlay::set_picker_action_error(std::string message) {
  tray_action_confirmation.reset();
  picker_action_error = std::move(message);
  full_redraw_requested = true;
}

void WorktreeManagementOverlay::refresh_worktree_picker() {
  if (mode != Mode::SWITCH_WORKTREE) {
    return;
  }
  tray_action_confirmation.reset();
  picker_action_error.clear();
  picker = nullptr;
  start_switch_worktree_picker();
  full_redraw_requested = true;
}

void WorktreeManagementOverlay::update_session_trays(
    std::vector<TraySnapshot> const& session_trays) {
  std::vector<TrayId> next_ids;
  next_ids.reserve(session_trays.size());
  for (TraySnapshot const& snapshot : session_trays) {
    if (snapshot.id.kind() == TrayIdKind::ANONYMOUS) {
      next_ids.push_back(snapshot.id);
    }
  }
  if (next_ids == session_tray_ids) {
    return;
  }
  session_tray_ids = std::move(next_ids);
  if (mode == Mode::SWITCH_WORKTREE && picker != nullptr) {
    picker = nullptr;
    start_switch_worktree_picker();
    full_redraw_requested = true;
  }
}

WorktreeManagementOverlay::Stage WorktreeManagementOverlay::current_stage() const {
  switch (mode) {
    case Mode::SWITCH_WORKTREE:
      return Stage::SWITCH_WORKTREE;
    case Mode::ADD_WORKTREE:
      return worktree_stage;
    case Mode::ADD_REPOSITORY:
      return repository_stage;
  }
  return Stage::SWITCH_WORKTREE;
}

WorktreeManagementOverlay::Stage& WorktreeManagementOverlay::mutable_current_stage() {
  if (mode == Mode::ADD_WORKTREE) {
    return worktree_stage;
  }
  if (mode == Mode::ADD_REPOSITORY) {
    return repository_stage;
  }
  throw std::logic_error("switch-worktree mode does not have a mutable stage");
}

WorktreeManagementOverlay::TextField* WorktreeManagementOverlay::active_text_field() {
  switch (current_stage()) {
    case Stage::WORKTREE_BRANCH:
      return &branch_field;
    case Stage::REPOSITORY_ROOT:
      return &repository_root_field;
    case Stage::REPOSITORY_CLONE_URL:
      return &clone_url_field;
    default:
      return nullptr;
  }
}

WorktreeManagementOverlay::TextField const* WorktreeManagementOverlay::active_text_field() const {
  switch (current_stage()) {
    case Stage::WORKTREE_BRANCH:
      return &branch_field;
    case Stage::REPOSITORY_ROOT:
      return &repository_root_field;
    case Stage::REPOSITORY_CLONE_URL:
      return &clone_url_field;
    default:
      return nullptr;
  }
}

std::string& WorktreeManagementOverlay::active_error_message() {
  switch (mode) {
    case Mode::SWITCH_WORKTREE:
      return switch_worktree_error_message;
    case Mode::ADD_WORKTREE:
      return worktree_error_message;
    case Mode::ADD_REPOSITORY:
      return repository_error_message;
  }
  return switch_worktree_error_message;
}

std::string const& WorktreeManagementOverlay::active_error_message() const {
  switch (mode) {
    case Mode::SWITCH_WORKTREE:
      return switch_worktree_error_message;
    case Mode::ADD_WORKTREE:
      return worktree_error_message;
    case Mode::ADD_REPOSITORY:
      return repository_error_message;
  }
  return switch_worktree_error_message;
}

std::optional<std::filesystem::path> WorktreeManagementOverlay::selected_repository_root() const {
  return selected_repository;
}

void WorktreeManagementOverlay::load_repositories() {
  repositories.clear();
  worktree_error_message.clear();
  try {
    persistence::WorktreeRegistry const registry = WorktreeRegistryStore(registry_path).load();
    for (persistence::Repository const& repository : registry.repositories()) {
      repositories.emplace_back(repository.root_path());
    }
    if (repositories.empty()) {
      worktree_error_message = "No registered repositories";
    }
  } catch (std::exception const& error) {
    worktree_error_message = error.what();
  }
}

void WorktreeManagementOverlay::reset_mode_state() {
  worktree_stage = Stage::WORKTREE_REPOSITORY;
  repository_stage = Stage::REPOSITORY_ROOT;
  branch_field = {};
  repository_root_field = {};
  clone_url_field = {};
  selected_repository.reset();
  repository_root.reset();
  pending_worktree_path.reset();
  switch_worktree_error_message.clear();
  repository_error_message.clear();
  transcript_lines.clear();
  transcript_line.clear();
  process_escape_sequence = false;
  process_control_sequence = false;
  result_succeeded = false;
  tray_action_confirmation.reset();
  picker_action_error.clear();
  load_repositories();
}

std::string WorktreeManagementOverlay::footer_output() const {
  return render_overlay_footer(MODE_LABELS, static_cast<std::size_t>(mode), size);
}

TerminalSize WorktreeManagementOverlay::dialog_terminal_size() const {
  return {
      .rows = std::max(size.rows - OVERLAY_FOOTER_HEIGHT, 1),
      .cols = std::max(size.cols, 1),
  };
}

void WorktreeManagementOverlay::cycle_mode(int const direction) {
  int constexpr MODE_COUNT = 3;
  int const current = static_cast<int>(mode);
  int const next = (current + direction + MODE_COUNT) % MODE_COUNT;
  reset_mode_state();
  mode = static_cast<Mode>(next);
  activate_mode();
}

void WorktreeManagementOverlay::activate_mode() {
  bool const picker_was_active = picker != nullptr;
  picker = nullptr;
  mode_switch_sequence.clear();
  input_sequence_state = InputSequenceState::NORMAL;
  input_control_sequence_parameters.clear();
  if (mode == Mode::SWITCH_WORKTREE) {
    start_switch_worktree_picker();
  } else if (mode == Mode::ADD_WORKTREE && worktree_stage == Stage::WORKTREE_REPOSITORY) {
    start_repository_picker();
  }
  if (picker_was_active && picker == nullptr) {
    full_redraw_requested = true;
  }
}

void WorktreeManagementOverlay::start_switch_worktree_picker() {
  try {
    std::vector<std::filesystem::path> const available =
        WorktreeCandidateFinder(git_executable).find_available(registry_path);
    std::set<std::filesystem::path> const available_paths(available.begin(), available.end());
    persistence::WorktreeRegistry const registry = WorktreeRegistryStore(registry_path).load();
    std::vector<std::filesystem::path> candidates;
    switch_candidate_tray_ids.clear();
    switch_candidate_available.clear();
    for (persistence::Repository const& repository : registry.repositories()) {
      for (persistence::Worktree const& worktree : repository.worktrees()) {
        std::filesystem::path const path(worktree.path());
        bool const is_available = available_paths.contains(path);
        candidates.emplace_back(is_available ? path.string() : path.string() + " [unavailable]");
        switch_candidate_tray_ids.push_back(TrayId::worktree(path));
        switch_candidate_available.push_back(is_available);
      }
    }
    for (TrayId const& tray_id : session_tray_ids) {
      if (tray_id.kind() != TrayIdKind::ANONYMOUS) {
        continue;
      }
      candidates.emplace_back("/anonymous/" + std::to_string(tray_id.anonymous_number().value()));
      switch_candidate_tray_ids.push_back(tray_id);
      switch_candidate_available.push_back(true);
    }
    if (candidates.empty()) {
      switch_worktree_error_message = "No available tracked worktrees";
      return;
    }
    switch_worktree_error_message.clear();
    picker =
        PathPickerOverlay::start(fzf_executable, candidates, "Worktree> ", dialog_terminal_size());
  } catch (std::exception const& error) {
    switch_worktree_error_message = error.what();
  }
}

void WorktreeManagementOverlay::start_repository_picker() {
  if (repositories.empty()) {
    worktree_error_message = "No registered repositories";
    return;
  }
  try {
    worktree_error_message.clear();
    picker = PathPickerOverlay::start(fzf_executable, repositories, "Repository> ",
                                      dialog_terminal_size());
  } catch (std::exception const& error) {
    worktree_error_message = error.what();
  }
}

void WorktreeManagementOverlay::write_mode_input(std::string_view const bytes) {
  if (picker != nullptr) {
    picker->write_input(bytes);
    return;
  }
  if (current_stage() == Stage::RESULT) {
    return;
  }
  if (current_stage() == Stage::SWITCH_WORKTREE) {
    if (bytes.find('\r') != std::string_view::npos || bytes.find('\n') != std::string_view::npos) {
      start_switch_worktree_picker();
    }
    return;
  }
  for (unsigned char const byte : bytes) {
    write_editing_input(byte);
  }
}

void WorktreeManagementOverlay::write_mode_switch_input(unsigned char const byte) {
  if (mode_switch_sequence.empty()) {
    if (byte == '\t') {
      cycle_mode(1);
      return;
    }
    if (byte == ESCAPE) {
      mode_switch_sequence.push_back(static_cast<char>(byte));
      return;
    }
    char const value = static_cast<char>(byte);
    write_mode_input(std::string_view(&value, 1));
    return;
  }

  mode_switch_sequence.push_back(static_cast<char>(byte));
  if (mode_switch_sequence.size() == 2U && byte == '[') {
    return;
  }
  if (mode_switch_sequence == "\x1b[Z") {
    mode_switch_sequence.clear();
    cycle_mode(-1);
    return;
  }
  if (mode_switch_sequence.size() == 2U || (byte >= 0x40U && byte <= 0x7EU)) {
    std::string sequence = std::exchange(mode_switch_sequence, {});
    write_mode_input(sequence);
  }
}

std::string WorktreeManagementOverlay::redraw_output() const {
  std::string output = footer_output();
  output += picker != nullptr ? picker->redraw_output() : dialog_redraw_output();
  output += picker_action_output();
  return output;
}

std::string WorktreeManagementOverlay::picker_action_output() const {
  if (!tray_action_confirmation.has_value() && picker_action_error.empty()) {
    return {};
  }

  std::string message;
  std::string background = "\x1b[48;5;52m";
  if (tray_action_confirmation.has_value()) {
    TrayId const& tray_id = tray_action_confirmation->tray_id;
    std::string const target =
        tray_id.kind() == TrayIdKind::ANONYMOUS
            ? "/anonymous/" + std::to_string(tray_id.anonymous_number().value())
            : tray_id.worktree_root().string();
    std::string_view const prefix =
        tray_action_confirmation->kind == TrayActionKind::CLEAR ? "Clear " : "Remove ";
    constexpr std::string_view SUFFIX = "? [y/N]";
    std::size_t const width = static_cast<std::size_t>(std::max(size.cols, 1));
    if (prefix.size() + target.size() + SUFFIX.size() <= width) {
      message = std::string(prefix) + target + std::string(SUFFIX);
    } else if (width > prefix.size() + SUFFIX.size() + 3U) {
      std::size_t const target_width = width - prefix.size() - SUFFIX.size() - 3U;
      message = std::string(prefix) + "..." + target.substr(target.size() - target_width) +
                std::string(SUFFIX);
    } else {
      message = tray_action_confirmation->kind == TrayActionKind::CLEAR ? "Clear? [y/N]"
                                                                        : "Remove? [y/N]";
    }
  } else {
    message = picker_action_error;
    background = "\x1b[48;5;88m";
  }

  int const columns = std::max(size.cols, 1);
  auto const width = static_cast<std::size_t>(columns);
  if (message.size() > width) {
    message = visible_tail(message, width);
  }
  message.resize(width, ' ');
  int const row = std::max(size.rows - OVERLAY_FOOTER_HEIGHT, 1);
  return position_cursor(row, 1) + background + "\x1b[38;5;255m" + message + "\x1b[0m";
}

std::string WorktreeManagementOverlay::dialog_redraw_output() const {
  int const available_rows = std::max(size.rows - OVERLAY_FOOTER_HEIGHT, 0);
  int const height = std::min(DIALOG_HEIGHT, available_rows);
  if (height == 0) {
    return {};
  }
  int const first_row = std::max(1, size.rows - height);
  int const columns = std::max(size.cols, 1);
  auto const width = static_cast<std::size_t>(columns);
  std::optional<std::filesystem::path> const selected_repository = selected_repository_root();
  std::vector<std::string> lines(static_cast<std::size_t>(height));
  std::size_t input_cursor_column = 1;
  auto const displayed_input = [&](TextField const& field) {
    std::string const prompt = "> " + field.value;
    std::size_t const prompt_cursor = 2U + std::min(field.cursor_offset, field.value.size());
    std::size_t const first_visible = prompt_cursor >= width ? prompt_cursor - width + 1U : 0U;
    input_cursor_column = prompt_cursor - first_visible + 1U;
    return prompt.substr(first_visible, width);
  };

  Stage const stage = current_stage();
  if (stage == Stage::SWITCH_WORKTREE) {
    if (height > 2) {
      lines[2] = switch_worktree_error_message;
    }
    if (height > 5) {
      lines[5] = "Enter: reopen selector | Tab: next mode";
    }
  } else if (stage == Stage::WORKTREE_REPOSITORY) {
    if (height > 1) {
      lines[1] = worktree_error_message;
    }
    if (height > 6) {
      lines[6] = "Enter: reopen selector | Tab: next mode";
    }
  } else if (stage == Stage::WORKTREE_BRANCH) {
    if (height > 1 && selected_repository.has_value()) {
      lines[1] = visible_tail(selected_repository->string(), width);
    }
    if (height > 2) {
      lines[2] = worktree_error_message;
    }
    if (height > 4) {
      lines[4] = "Branch:";
    }
    if (height > 5 && active_text_field() != nullptr) {
      lines[5] = displayed_input(*active_text_field());
    }
    if (height > 6) {
      lines[6] = "Enter: continue | Tab: mode";
    }
  } else if (stage == Stage::REPOSITORY_ROOT || stage == Stage::REPOSITORY_CLONE_URL) {
    if (height > 1 && stage == Stage::REPOSITORY_CLONE_URL && repository_root.has_value()) {
      lines[1] = visible_tail(repository_root->string(), width);
    }
    if (height > 2) {
      lines[2] = repository_error_message;
    }
    if (height > 4) {
      lines[4] = stage == Stage::REPOSITORY_ROOT ? "Repository root:" : "Clone URL:";
    }
    if (height > 5 && active_text_field() != nullptr) {
      lines[5] = displayed_input(*active_text_field());
    }
    if (height > 6) {
      lines[6] = "Enter: continue | Tab: mode";
    }
  } else {
    std::size_t const transcript_capacity = height > 2 ? static_cast<std::size_t>(height - 2) : 0U;
    std::size_t const transcript_start = transcript_lines.size() > transcript_capacity
                                             ? transcript_lines.size() - transcript_capacity
                                             : 0U;
    std::size_t output_row = 1;
    for (std::size_t index = transcript_start;
         index < transcript_lines.size() && output_row + 1U < static_cast<std::size_t>(height);
         ++index, ++output_row) {
      lines[output_row] = transcript_lines[index];
    }
    if (!transcript_line.empty() && output_row + 1U < static_cast<std::size_t>(height)) {
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
    append_filled_line(output, first_row + offset, lines[static_cast<std::size_t>(offset)],
                       columns);
  }

  if (active_text_field() != nullptr && stage != Stage::RUNNING && stage != Stage::RESULT &&
      height > 5) {
    output += position_cursor(first_row + 5, static_cast<int>(input_cursor_column));
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
    } else if (byte >= 0x20U && byte <= 0x3FU && input_control_sequence_parameters.size() < 16U) {
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

  TextField* const field = active_text_field();
  if (field == nullptr) {
    return;
  }
  if (byte == BACKSPACE || byte == '\b') {
    erase_before_input_cursor();
    active_error_message().clear();
    return;
  }
  if (byte >= 0x20U) {
    field->value.insert(field->cursor_offset, 1U, static_cast<char>(byte));
    ++field->cursor_offset;
    active_error_message().clear();
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
    if (TextField* const field = active_text_field(); field != nullptr) {
      field->cursor_offset = 0;
    }
  } else if (final_byte == 'F' ||
             (final_byte == '~' && (input_control_sequence_parameters == "4" ||
                                    input_control_sequence_parameters == "8"))) {
    if (TextField* const field = active_text_field(); field != nullptr) {
      field->cursor_offset = field->value.size();
    }
  } else if (final_byte == '~' && input_control_sequence_parameters == "3") {
    erase_at_input_cursor();
  }
}

void WorktreeManagementOverlay::move_input_cursor_left() {
  if (TextField* const field = active_text_field(); field != nullptr) {
    field->cursor_offset = previous_utf8_code_point_start(field->value, field->cursor_offset);
  }
}

void WorktreeManagementOverlay::move_input_cursor_right() {
  if (TextField* const field = active_text_field(); field != nullptr) {
    field->cursor_offset = next_utf8_code_point_end(field->value, field->cursor_offset);
  }
}

void WorktreeManagementOverlay::erase_before_input_cursor() {
  TextField* const field = active_text_field();
  if (field == nullptr) {
    return;
  }
  std::size_t const previous = previous_utf8_code_point_start(field->value, field->cursor_offset);
  field->value.erase(previous, field->cursor_offset - previous);
  field->cursor_offset = previous;
}

void WorktreeManagementOverlay::erase_at_input_cursor() {
  TextField* const field = active_text_field();
  if (field == nullptr) {
    return;
  }
  std::size_t const next = next_utf8_code_point_end(field->value, field->cursor_offset);
  field->value.erase(field->cursor_offset, next - field->cursor_offset);
}

void WorktreeManagementOverlay::submit_input() {
  switch (current_stage()) {
    case Stage::WORKTREE_REPOSITORY:
      submit_worktree_repository();
      break;
    case Stage::WORKTREE_BRANCH:
      submit_worktree_branch();
      break;
    case Stage::REPOSITORY_ROOT:
      submit_repository_root();
      break;
    case Stage::REPOSITORY_CLONE_URL:
      submit_clone_url();
      break;
    default:
      break;
  }
}

void WorktreeManagementOverlay::submit_worktree_repository() {
  if (!selected_repository_root().has_value()) {
    start_repository_picker();
    return;
  }
  worktree_error_message.clear();
  worktree_stage = Stage::WORKTREE_BRANCH;
}

void WorktreeManagementOverlay::submit_worktree_branch() {
  std::string const branch = trimmed(branch_field.value);
  try {
    std::optional<std::filesystem::path> const selected_repository = selected_repository_root();
    if (!selected_repository.has_value()) {
      throw std::logic_error("selected repository is missing");
    }
    pending_worktree_path = derived_worktree_path(*selected_repository, branch);
    branch_field.value = branch;
    branch_field.cursor_offset = branch_field.value.size();
    worktree_error_message.clear();
    start_worktree_provision();
  } catch (std::exception const& error) {
    worktree_error_message = error.what();
  }
}

void WorktreeManagementOverlay::submit_repository_root() {
  try {
    repository_root = resolved_path(repository_root_field, "repository root");
    RepositoryRootState const state = inspect_repository_root(*repository_root);
    repository_error_message.clear();
    if (state == RepositoryRootState::EMPTY) {
      repository_stage = Stage::REPOSITORY_CLONE_URL;
      return;
    }
    start_registration(std::nullopt);
  } catch (std::exception const& error) {
    repository_error_message = error.what();
  }
}

void WorktreeManagementOverlay::submit_clone_url() {
  std::string const clone_url = trimmed(clone_url_field.value);
  if (clone_url.empty()) {
    repository_error_message = "Clone URL must not be empty";
    return;
  }
  repository_error_message.clear();
  try {
    start_registration(clone_url);
  } catch (std::exception const& error) {
    repository_error_message = error.what();
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
  repository_stage = Stage::RUNNING;
}

void WorktreeManagementOverlay::start_worktree_provision() {
  std::optional<std::filesystem::path> const selected_repository = selected_repository_root();
  if (!selected_repository.has_value() || !pending_worktree_path.has_value()) {
    throw std::logic_error("worktree provision inputs are missing");
  }
  std::vector<std::string> const command{
      parent_executable.string(),    "--provision-worktree", registry_path.string(),
      selected_repository->string(), branch_field.value,     pending_worktree_path->string(),
  };

  transcript_lines.clear();
  transcript_line.clear();
  process = ContentPtySession::start(command, working_directory, size);
  worktree_stage = Stage::RUNNING;
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

std::filesystem::path WorktreeManagementOverlay::resolved_path(
    TextField const& field, std::string const& description) const {
  std::string value = trimmed(field.value);
  if (value.empty()) {
    throw std::invalid_argument(description + " must not be empty");
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
    throw std::invalid_argument("Failed to resolve " + description + ": " + path.string());
  }
  return normalized;
}

}  // namespace moe::parent
