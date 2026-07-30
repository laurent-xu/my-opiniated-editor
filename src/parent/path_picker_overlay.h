#pragma once

#include <cstddef>
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

class PathPickerOverlay : public Overlay {
 public:
  static std::unique_ptr<PathPickerOverlay> start(
      std::string fzf_executable, std::vector<std::filesystem::path> const& candidates,
      std::string prompt, TerminalSize parent_size);

  PathPickerOverlay(PathPickerOverlay const&) = delete;
  PathPickerOverlay& operator=(PathPickerOverlay const&) = delete;
  ~PathPickerOverlay() override;

  void write_input(std::string_view bytes) override;
  [[nodiscard]] bool read_process_output() override;
  [[nodiscard]] bool refresh_process_state() override;
  void resize(TerminalSize size) override;
  [[nodiscard]] std::optional<base::FileDescriptor> process_file_descriptor() const override;
  [[nodiscard]] std::string redraw_output() const override;

  [[nodiscard]] bool finished() const noexcept;
  [[nodiscard]] std::optional<std::filesystem::path> const& selected_path() const noexcept;
  [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
  [[nodiscard]] std::optional<std::size_t> highlighted_index() const noexcept;
  [[nodiscard]] int first_row() const noexcept;

 private:
  struct Handles {
    base::OwnedFileDescriptor terminal_master;
    base::OwnedFileDescriptor candidate_input;
    base::OwnedFileDescriptor result_output;
    base::ProcessId child_pid;
  };

  PathPickerOverlay(Handles handles, std::vector<std::filesystem::path> candidates,
                    TerminalSize parent_size);

  void write_candidates();
  void ingest_focus_notifications(std::string_view bytes);
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
  std::optional<std::size_t> selection_index;
  std::optional<std::size_t> highlighted_candidate_index;
  std::string pending_focus_notification_bytes;
};

}  // namespace moe::parent
