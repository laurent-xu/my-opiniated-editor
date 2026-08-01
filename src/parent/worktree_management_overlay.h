#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/base/terminal_size.h"
#include "src/parent/overlay.h"
#include "src/parent/tray_action_kind.h"
#include "src/parent/tray_action_request.h"
#include "src/parent/tray_preview_request.h"
#include "src/parent/tray_snapshot.h"

namespace moe::parent {

class ContentPtySession;
class PathPickerOverlay;

class WorktreeManagementOverlay : public Overlay {
 public:
  static std::unique_ptr<WorktreeManagementOverlay> start(
      std::filesystem::path parent_executable, std::filesystem::path registry_path,
      std::filesystem::path working_directory, std::string git_executable,
      std::string fzf_executable, std::vector<TraySnapshot> const& session_trays,
      base::TerminalSize size);

  WorktreeManagementOverlay(WorktreeManagementOverlay const&) = delete;
  WorktreeManagementOverlay& operator=(WorktreeManagementOverlay const&) = delete;
  ~WorktreeManagementOverlay() override;

  void write_input(std::string_view bytes) override;
  [[nodiscard]] bool read_process_output() override;
  [[nodiscard]] bool refresh_process_state() override;
  void resize(base::TerminalSize size) override;

  [[nodiscard]] std::optional<base::FileDescriptor> process_file_descriptor() const override;
  [[nodiscard]] std::string redraw_output() const override;
  [[nodiscard]] bool take_full_redraw_request() noexcept;
  [[nodiscard]] std::optional<TrayId> take_tray_to_open();
  [[nodiscard]] std::optional<TrayPreviewRequest> preview_request() const;
  [[nodiscard]] std::optional<TrayId> highlighted_tray_id() const;
  [[nodiscard]] bool begin_tray_action_confirmation(TrayActionKind kind);
  [[nodiscard]] bool has_tray_action_confirmation() const noexcept;
  [[nodiscard]] std::optional<TrayActionRequest> resolve_tray_action_confirmation(bool confirmed);
  void cancel_tray_action_confirmation();
  void set_picker_action_error(std::string message);
  void refresh_worktree_picker();
  void update_session_trays(std::vector<TraySnapshot> const& session_trays);

 private:
  enum class Mode : std::uint8_t {
    SWITCH_WORKTREE,
    ADD_WORKTREE,
    ADD_REPOSITORY,
  };

  enum class Stage : std::uint8_t {
    SWITCH_WORKTREE,
    WORKTREE_REPOSITORY,
    WORKTREE_BRANCH,
    REPOSITORY_ROOT,
    REPOSITORY_CLONE_URL,
    RUNNING,
    RESULT,
  };

  enum class InputSequenceState : std::uint8_t {
    NORMAL,
    ESCAPE,
    CONTROL_SEQUENCE,
  };

  struct TextField {
    std::string value;
    std::size_t cursor_offset = 0;
  };

  WorktreeManagementOverlay(std::filesystem::path parent_executable,
                            std::filesystem::path registry_path,
                            std::filesystem::path working_directory, std::string git_executable,
                            std::string fzf_executable,
                            std::vector<TraySnapshot> const& session_trays,
                            base::TerminalSize size);

  [[nodiscard]] Stage current_stage() const;
  [[nodiscard]] Stage& mutable_current_stage();
  [[nodiscard]] TextField* active_text_field();
  [[nodiscard]] TextField const* active_text_field() const;
  [[nodiscard]] std::string& active_error_message();
  [[nodiscard]] std::string const& active_error_message() const;
  [[nodiscard]] std::string footer_output() const;
  [[nodiscard]] base::TerminalSize dialog_terminal_size() const;
  [[nodiscard]] std::string dialog_redraw_output() const;
  [[nodiscard]] std::string picker_action_output() const;
  [[nodiscard]] std::optional<std::filesystem::path> selected_repository_root() const;
  void load_repositories();
  void reset_mode_state();
  void cycle_mode(int direction);
  void activate_mode();
  void start_switch_worktree_picker();
  void start_repository_picker();
  void write_mode_input(std::string_view bytes);
  void write_mode_switch_input(unsigned char byte);
  void write_editing_input(unsigned char byte);
  void handle_input_control_sequence(unsigned char final_byte);
  void move_input_cursor_left();
  void move_input_cursor_right();
  void erase_before_input_cursor();
  void erase_at_input_cursor();
  void submit_input();
  void submit_worktree_repository();
  void submit_worktree_branch();
  void submit_repository_root();
  void submit_clone_url();
  void start_registration(std::optional<std::string> clone_url);
  void start_worktree_provision();
  void append_process_output(std::string_view bytes);
  void append_transcript_line();
  [[nodiscard]] std::filesystem::path resolved_path(TextField const& field,
                                                    std::string const& description) const;

  std::filesystem::path parent_executable;
  std::filesystem::path registry_path;
  std::filesystem::path working_directory;
  std::string git_executable;
  std::string fzf_executable;
  base::TerminalSize size;
  Mode mode = Mode::SWITCH_WORKTREE;
  Stage worktree_stage = Stage::WORKTREE_REPOSITORY;
  Stage repository_stage = Stage::REPOSITORY_ROOT;
  TextField branch_field;
  TextField repository_root_field;
  TextField clone_url_field;
  std::string mode_switch_sequence;
  InputSequenceState input_sequence_state = InputSequenceState::NORMAL;
  std::string input_control_sequence_parameters;
  std::string switch_worktree_error_message;
  std::string worktree_error_message;
  std::string repository_error_message;
  std::vector<std::filesystem::path> repositories;
  std::vector<TrayId> session_tray_ids;
  std::vector<TrayId> switch_candidate_tray_ids;
  std::vector<bool> switch_candidate_available;
  std::optional<std::filesystem::path> selected_repository;
  std::optional<std::filesystem::path> repository_root;
  std::optional<std::filesystem::path> pending_worktree_path;
  std::optional<TrayId> tray_to_open;
  std::optional<TrayActionRequest> tray_action_confirmation;
  std::string picker_action_error;
  std::unique_ptr<PathPickerOverlay> picker;
  std::unique_ptr<ContentPtySession> process;
  std::vector<std::string> transcript_lines;
  std::string transcript_line;
  bool process_escape_sequence = false;
  bool process_control_sequence = false;
  bool result_succeeded = false;
  bool full_redraw_requested = false;
};

}  // namespace moe::parent
