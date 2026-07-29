#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/owned_file_descriptor.h"
#include "src/base/process_id.h"
#include "src/parent/overlay.h"
#include "src/parent/terminal_screen.h"

namespace moe::parent {

[[nodiscard]] std::string configured_fzf_executable();

class WorktreePickerOverlay : public Overlay {
 public:
  static std::unique_ptr<WorktreePickerOverlay> start(
      std::string fzf_executable, std::vector<std::filesystem::path> const& candidates,
      TerminalSize parent_size);

  WorktreePickerOverlay(WorktreePickerOverlay const&) = delete;
  WorktreePickerOverlay& operator=(WorktreePickerOverlay const&) = delete;
  ~WorktreePickerOverlay() override;

  void write_input(std::string_view bytes) override;
  [[nodiscard]] bool read_process_output() override;
  [[nodiscard]] bool refresh_process_state() override;
  void resize(TerminalSize size) override;
  [[nodiscard]] std::optional<base::FileDescriptor> process_file_descriptor() const override;
  [[nodiscard]] std::string redraw_output() const override;

  [[nodiscard]] bool finished() const noexcept;
  [[nodiscard]] std::optional<std::filesystem::path> const& selected_worktree() const noexcept;

 private:
  struct Handles {
    base::OwnedFileDescriptor terminal_master;
    base::OwnedFileDescriptor candidate_input;
    base::OwnedFileDescriptor result_output;
    base::ProcessId child_pid;
  };

  WorktreePickerOverlay(Handles handles, std::vector<std::filesystem::path> candidates,
                        TerminalSize parent_size);

  void write_candidates();
  void read_selection();
  void reset_process() noexcept;
  [[nodiscard]] static TerminalSize picker_size_for(TerminalSize parent_size);

  base::OwnedFileDescriptor terminal_master;
  base::OwnedFileDescriptor candidate_input;
  base::OwnedFileDescriptor result_output;
  base::ProcessId child_process_id;
  std::vector<std::filesystem::path> candidate_paths;
  TerminalSize parent_terminal_size;
  TerminalSize picker_terminal_size;
  TerminalScreen terminal_screen;
  bool process_finished = false;
  std::optional<std::filesystem::path> selection;
};

}  // namespace moe::parent
