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
#include "src/parent/overlay/overlay.h"
#include "src/parent/tray/tray_action_kind.h"
#include "src/parent/tray/tray_action_request.h"
#include "src/parent/tray/tray_preview_request.h"
#include "src/parent/tray/tray_snapshot.h"
#include "src/parent/worktree/overlay/terminal_text_field.h"
#include "src/parent/worktree/overlay/workflow/worktree_overlay_mode_direction.h"
#include "src/parent/worktree/overlay/workflow/worktree_overlay_workflow_state.h"

namespace moe::parent {

class PathPickerOverlay;
class WorktreeOverlayProcess;

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
  [[nodiscard]] int opaque_region_start_row() const noexcept;
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
  enum class InputSequenceState : std::uint8_t {
    NORMAL,
    ESCAPE,
    CONTROL_SEQUENCE,
  };

  WorktreeManagementOverlay(std::filesystem::path parent_executable,
                            std::filesystem::path registry_path,
                            std::filesystem::path working_directory, std::string git_executable,
                            std::string fzf_executable,
                            std::vector<TraySnapshot> const& session_trays,
                            base::TerminalSize size);

  [[nodiscard]] std::string footer_output() const;
  [[nodiscard]] base::TerminalSize dialog_terminal_size() const;
  [[nodiscard]] std::string dialog_redraw_output() const;
  [[nodiscard]] std::string picker_action_output() const;
  [[nodiscard]] std::optional<std::filesystem::path> selected_repository_root() const;
  void load_repositories();
  void cycle_mode(WorktreeOverlayModeDirection direction);
  void activate_mode();
  void start_switch_worktree_picker();
  void start_repository_picker();
  void write_mode_input(std::string_view bytes);
  void write_mode_switch_input(unsigned char byte);
  void write_editing_input(unsigned char byte);
  void handle_input_control_sequence(unsigned char final_byte);
  void submit_input();
  void submit_worktree_repository();
  void submit_worktree_branch();
  void submit_repository_root();
  void submit_clone_url();
  void start_registration(std::optional<std::string> clone_url);
  void start_worktree_provision();
  [[nodiscard]] std::filesystem::path resolved_path(TerminalTextField const& field,
                                                    std::string const& description) const;

  std::filesystem::path parent_executable;
  std::filesystem::path registry_path;
  std::filesystem::path working_directory;
  std::string git_executable;
  std::string fzf_executable;
  base::TerminalSize size;
  WorktreeOverlayWorkflowState workflow_state;
  std::string mode_switch_sequence;
  InputSequenceState input_sequence_state = InputSequenceState::NORMAL;
  std::string input_control_sequence_parameters;
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
  std::unique_ptr<WorktreeOverlayProcess> helper_process;
  bool full_redraw_requested = false;
};

}  // namespace moe::parent
