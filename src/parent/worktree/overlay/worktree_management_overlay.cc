#include "src/parent/worktree/overlay/worktree_management_overlay.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/parent/overlay/overlay_footer.h"
#include "src/parent/overlay/path_picker_overlay.h"
#include "src/parent/tray/tray_action_kind.h"
#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/worktree/overlay/worktree_overlay_process.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"
#include "src/parent/worktree/repository_root_state.h"
#include "src/parent/worktree/worktree_candidate_finder.h"
#include "src/parent/worktree/worktree_provisioner.h"
#include "src/parent/worktree/worktree_repository_registrar.h"

namespace moe::parent {

using base::TerminalSize;
using Mode = WorktreeOverlayMode;
using Stage = WorktreeOverlayStage;

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
      size(initial_size),
      helper_process(std::make_unique<WorktreeOverlayProcess>()) {
  update_session_trays(session_trays);
  load_repositories();
  activate_mode();
}

WorktreeManagementOverlay::~WorktreeManagementOverlay() = default;

void WorktreeManagementOverlay::write_input(std::string_view const bytes) {
  if (workflow_state.current_stage() == Stage::RUNNING) {
    helper_process->write(bytes);
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
  return helper_process->read_process_output();
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
      workflow_state.active_error_message() = "No path selected";
      return true;
    }
    if (workflow_state.mode() == Mode::SWITCH_WORKTREE) {
      if (!selected_index.has_value() || *selected_index >= switch_candidate_tray_ids.size() ||
          *selected_index >= switch_candidate_available.size()) {
        workflow_state.error_message(Mode::SWITCH_WORKTREE) = "Selected tray is unavailable";
        return true;
      }
      if (!switch_candidate_available[*selected_index]) {
        start_switch_worktree_picker();
        set_picker_action_error("Worktree is unavailable; use Shift+R to remove it");
        return true;
      }
      tray_to_open = switch_candidate_tray_ids[*selected_index];
      workflow_state.error_message(Mode::SWITCH_WORKTREE).clear();
    } else if (workflow_state.mode() == Mode::ADD_WORKTREE) {
      selected_repository = *selected;
      workflow_state.error_message(Mode::ADD_WORKTREE).clear();
      workflow_state.set_current_stage(Stage::WORKTREE_BRANCH);
    }
    return true;
  }
  if (workflow_state.current_stage() != Stage::RUNNING ||
      !helper_process->refresh_process_state()) {
    return false;
  }
  workflow_state.set_current_stage(Stage::RESULT);
  if (helper_process->result_succeeded() && workflow_state.mode() == Mode::ADD_WORKTREE) {
    if (!pending_worktree_path.has_value()) {
      throw std::logic_error("successful worktree provision is missing its path");
    }
    tray_to_open = TrayId::worktree(pending_worktree_path.value());
  }
  return true;
}

void WorktreeManagementOverlay::resize(TerminalSize const next_size) {
  size = next_size;
  helper_process->resize(next_size);
  if (picker != nullptr) {
    picker->resize(dialog_terminal_size());
  }
}

std::optional<base::FileDescriptor> WorktreeManagementOverlay::process_file_descriptor() const {
  if (picker != nullptr) {
    return picker->process_file_descriptor();
  }
  return helper_process->file_descriptor();
}

bool WorktreeManagementOverlay::take_full_redraw_request() noexcept {
  return std::exchange(full_redraw_requested, false);
}

std::optional<TrayId> WorktreeManagementOverlay::take_tray_to_open() {
  return std::exchange(tray_to_open, std::nullopt);
}

std::optional<TrayPreviewRequest> WorktreeManagementOverlay::preview_request() const {
  if (workflow_state.mode() != Mode::SWITCH_WORKTREE || picker == nullptr) {
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
  if (workflow_state.mode() != Mode::SWITCH_WORKTREE || picker == nullptr) {
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
  if (workflow_state.mode() != Mode::SWITCH_WORKTREE) {
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
  if (workflow_state.mode() == Mode::SWITCH_WORKTREE && picker != nullptr) {
    picker = nullptr;
    start_switch_worktree_picker();
    full_redraw_requested = true;
  }
}

std::optional<std::filesystem::path> WorktreeManagementOverlay::selected_repository_root() const {
  return selected_repository;
}

void WorktreeManagementOverlay::load_repositories() {
  repositories.clear();
  workflow_state.error_message(Mode::ADD_WORKTREE).clear();
  try {
    persistence::WorktreeRegistry const registry = WorktreeRegistryStore(registry_path).load();
    for (persistence::Repository const& repository : registry.repositories()) {
      repositories.emplace_back(repository.root_path());
    }
    if (repositories.empty()) {
      workflow_state.error_message(Mode::ADD_WORKTREE) = "No registered repositories";
    }
  } catch (std::exception const& error) {
    workflow_state.error_message(Mode::ADD_WORKTREE) = error.what();
  }
}

std::string WorktreeManagementOverlay::footer_output() const {
  return render_overlay_footer(MODE_LABELS, static_cast<std::size_t>(workflow_state.mode()), size);
}

TerminalSize WorktreeManagementOverlay::dialog_terminal_size() const {
  return {
      .rows = std::max(size.rows - OVERLAY_FOOTER_HEIGHT, 1),
      .cols = std::max(size.cols, 1),
  };
}

void WorktreeManagementOverlay::cycle_mode(int const direction) {
  selected_repository.reset();
  repository_root.reset();
  pending_worktree_path.reset();
  helper_process->clear();
  tray_action_confirmation.reset();
  picker_action_error.clear();
  workflow_state.cycle_mode(direction);
  load_repositories();
  activate_mode();
}

void WorktreeManagementOverlay::activate_mode() {
  bool const picker_was_active = picker != nullptr;
  picker = nullptr;
  mode_switch_sequence.clear();
  input_sequence_state = InputSequenceState::NORMAL;
  input_control_sequence_parameters.clear();
  if (workflow_state.mode() == Mode::SWITCH_WORKTREE) {
    start_switch_worktree_picker();
  } else if (workflow_state.mode() == Mode::ADD_WORKTREE &&
             workflow_state.current_stage() == Stage::WORKTREE_REPOSITORY) {
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
      workflow_state.error_message(Mode::SWITCH_WORKTREE) = "No available tracked worktrees";
      return;
    }
    workflow_state.error_message(Mode::SWITCH_WORKTREE).clear();
    picker =
        PathPickerOverlay::start(fzf_executable, candidates, "Worktree> ", dialog_terminal_size());
  } catch (std::exception const& error) {
    workflow_state.error_message(Mode::SWITCH_WORKTREE) = error.what();
  }
}

void WorktreeManagementOverlay::start_repository_picker() {
  if (repositories.empty()) {
    workflow_state.error_message(Mode::ADD_WORKTREE) = "No registered repositories";
    return;
  }
  try {
    workflow_state.error_message(Mode::ADD_WORKTREE).clear();
    picker = PathPickerOverlay::start(fzf_executable, repositories, "Repository> ",
                                      dialog_terminal_size());
  } catch (std::exception const& error) {
    workflow_state.error_message(Mode::ADD_WORKTREE) = error.what();
  }
}

void WorktreeManagementOverlay::write_mode_input(std::string_view const bytes) {
  if (picker != nullptr) {
    picker->write_input(bytes);
    return;
  }
  if (workflow_state.current_stage() == Stage::RESULT) {
    return;
  }
  if (workflow_state.current_stage() == Stage::SWITCH_WORKTREE) {
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
  auto const displayed_input = [&](TerminalTextField const& field) {
    std::string const prompt = "> " + field.value();
    std::size_t const prompt_cursor = 2U + std::min(field.cursor_offset(), field.value().size());
    std::size_t const first_visible = prompt_cursor >= width ? prompt_cursor - width + 1U : 0U;
    input_cursor_column = prompt_cursor - first_visible + 1U;
    return prompt.substr(first_visible, width);
  };

  Stage const stage = workflow_state.current_stage();
  if (stage == Stage::SWITCH_WORKTREE) {
    if (height > 2) {
      lines[2] = workflow_state.error_message(Mode::SWITCH_WORKTREE);
    }
    if (height > 5) {
      lines[5] = "Enter: reopen selector | Tab: next mode";
    }
  } else if (stage == Stage::WORKTREE_REPOSITORY) {
    if (height > 1) {
      lines[1] = workflow_state.error_message(Mode::ADD_WORKTREE);
    }
    if (height > 6) {
      lines[6] = "Enter: reopen selector | Tab: next mode";
    }
  } else if (stage == Stage::WORKTREE_BRANCH) {
    if (height > 1 && selected_repository.has_value()) {
      lines[1] = visible_tail(selected_repository->string(), width);
    }
    if (height > 2) {
      lines[2] = workflow_state.error_message(Mode::ADD_WORKTREE);
    }
    if (height > 4) {
      lines[4] = "Branch:";
    }
    if (height > 5 && workflow_state.active_text_field() != nullptr) {
      lines[5] = displayed_input(*workflow_state.active_text_field());
    }
    if (height > 6) {
      lines[6] = "Enter: continue | Tab: mode";
    }
  } else if (stage == Stage::REPOSITORY_ROOT || stage == Stage::REPOSITORY_CLONE_URL) {
    if (height > 1 && stage == Stage::REPOSITORY_CLONE_URL && repository_root.has_value()) {
      lines[1] = visible_tail(repository_root->string(), width);
    }
    if (height > 2) {
      lines[2] = workflow_state.error_message(Mode::ADD_REPOSITORY);
    }
    if (height > 4) {
      lines[4] = stage == Stage::REPOSITORY_ROOT ? "Repository root:" : "Clone URL:";
    }
    if (height > 5 && workflow_state.active_text_field() != nullptr) {
      lines[5] = displayed_input(*workflow_state.active_text_field());
    }
    if (height > 6) {
      lines[6] = "Enter: continue | Tab: mode";
    }
  } else {
    std::vector<std::string> const& transcript_lines = helper_process->transcript_lines();
    std::string const& transcript_line = helper_process->transcript_line();
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
        result_line = helper_process->result_succeeded() ? "Completed" : "Failed";
      }
      lines[static_cast<std::size_t>(height - 1)] = std::move(result_line);
    }
  }

  std::string output("\x1b[?25l");
  for (int offset = 0; offset < height; ++offset) {
    append_filled_line(output, first_row + offset, lines[static_cast<std::size_t>(offset)],
                       columns);
  }

  if (workflow_state.active_text_field() != nullptr && stage != Stage::RUNNING &&
      stage != Stage::RESULT && height > 5) {
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

  TerminalTextField* const field = workflow_state.active_text_field();
  if (field == nullptr) {
    return;
  }
  if (byte == BACKSPACE || byte == '\b') {
    field->backspace();
    workflow_state.active_error_message().clear();
    return;
  }
  if (byte >= 0x20U) {
    field->insert(byte);
    workflow_state.active_error_message().clear();
  }
}

void WorktreeManagementOverlay::handle_input_control_sequence(unsigned char const final_byte) {
  if (final_byte == 'D') {
    if (TerminalTextField* const field = workflow_state.active_text_field(); field != nullptr) {
      field->move_cursor_left();
    }
  } else if (final_byte == 'C') {
    if (TerminalTextField* const field = workflow_state.active_text_field(); field != nullptr) {
      field->move_cursor_right();
    }
  } else if (final_byte == 'H' ||
             (final_byte == '~' && (input_control_sequence_parameters == "1" ||
                                    input_control_sequence_parameters == "7"))) {
    if (TerminalTextField* const field = workflow_state.active_text_field(); field != nullptr) {
      field->move_cursor_to_start();
    }
  } else if (final_byte == 'F' ||
             (final_byte == '~' && (input_control_sequence_parameters == "4" ||
                                    input_control_sequence_parameters == "8"))) {
    if (TerminalTextField* const field = workflow_state.active_text_field(); field != nullptr) {
      field->move_cursor_to_end();
    }
  } else if (final_byte == '~' && input_control_sequence_parameters == "3") {
    if (TerminalTextField* const field = workflow_state.active_text_field(); field != nullptr) {
      field->delete_at_cursor();
    }
  }
}

void WorktreeManagementOverlay::submit_input() {
  switch (workflow_state.current_stage()) {
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
  workflow_state.error_message(Mode::ADD_WORKTREE).clear();
  workflow_state.set_current_stage(Stage::WORKTREE_BRANCH);
}

void WorktreeManagementOverlay::submit_worktree_branch() {
  std::string const branch = trimmed(workflow_state.branch_field().value());
  try {
    std::optional<std::filesystem::path> const selected_repository = selected_repository_root();
    if (!selected_repository.has_value()) {
      throw std::logic_error("selected repository is missing");
    }
    pending_worktree_path = derived_worktree_path(*selected_repository, branch);
    workflow_state.branch_field().set_value(branch);
    workflow_state.error_message(Mode::ADD_WORKTREE).clear();
    start_worktree_provision();
  } catch (std::exception const& error) {
    workflow_state.error_message(Mode::ADD_WORKTREE) = error.what();
  }
}

void WorktreeManagementOverlay::submit_repository_root() {
  try {
    repository_root = resolved_path(workflow_state.repository_root_field(), "repository root");
    RepositoryRootState const state = inspect_repository_root(*repository_root);
    workflow_state.error_message(Mode::ADD_REPOSITORY).clear();
    if (state == RepositoryRootState::EMPTY) {
      workflow_state.set_current_stage(Stage::REPOSITORY_CLONE_URL);
      return;
    }
    start_registration(std::nullopt);
  } catch (std::exception const& error) {
    workflow_state.error_message(Mode::ADD_REPOSITORY) = error.what();
  }
}

void WorktreeManagementOverlay::submit_clone_url() {
  std::string const clone_url = trimmed(workflow_state.clone_url_field().value());
  if (clone_url.empty()) {
    workflow_state.error_message(Mode::ADD_REPOSITORY) = "Clone URL must not be empty";
    return;
  }
  workflow_state.error_message(Mode::ADD_REPOSITORY).clear();
  try {
    start_registration(clone_url);
  } catch (std::exception const& error) {
    workflow_state.error_message(Mode::ADD_REPOSITORY) = error.what();
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

  helper_process->start(command, working_directory, size);
  workflow_state.set_current_stage(Stage::RUNNING);
}

void WorktreeManagementOverlay::start_worktree_provision() {
  std::optional<std::filesystem::path> const selected_repository = selected_repository_root();
  if (!selected_repository.has_value() || !pending_worktree_path.has_value()) {
    throw std::logic_error("worktree provision inputs are missing");
  }
  std::vector<std::string> const command{
      parent_executable.string(),
      "--provision-worktree",
      registry_path.string(),
      selected_repository->string(),
      workflow_state.branch_field().value(),
      pending_worktree_path->string(),
  };

  helper_process->start(command, working_directory, size);
  workflow_state.set_current_stage(Stage::RUNNING);
}

std::filesystem::path WorktreeManagementOverlay::resolved_path(
    TerminalTextField const& field, std::string const& description) const {
  std::string value = trimmed(field.value());
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
