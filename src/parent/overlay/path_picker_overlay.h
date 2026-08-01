#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/terminal_size.h"
#include "src/parent/overlay/overlay.h"

namespace moe::parent {

class PathPickerProcess;
class TerminalScreen;

[[nodiscard]] std::string configured_fzf_executable();

class PathPickerOverlay : public Overlay {
 public:
  static std::unique_ptr<PathPickerOverlay> start(
      std::string fzf_executable, std::vector<std::filesystem::path> const& candidates,
      std::string prompt, base::TerminalSize parent_size);

  PathPickerOverlay(PathPickerOverlay const&) = delete;
  PathPickerOverlay& operator=(PathPickerOverlay const&) = delete;
  ~PathPickerOverlay() override;

  void write_input(std::string_view bytes) override;
  [[nodiscard]] bool read_process_output() override;
  [[nodiscard]] bool refresh_process_state() override;
  void resize(base::TerminalSize size) override;
  [[nodiscard]] std::optional<base::FileDescriptor> process_file_descriptor() const override;
  [[nodiscard]] std::string redraw_output() const override;

  [[nodiscard]] bool finished() const noexcept;
  [[nodiscard]] std::optional<std::filesystem::path> const& selected_path() const noexcept;
  [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
  [[nodiscard]] std::optional<std::size_t> highlighted_index() const noexcept;
  [[nodiscard]] base::TerminalSize available_region_above() const noexcept;

 private:
  PathPickerOverlay(std::unique_ptr<PathPickerProcess> process,
                    std::vector<std::filesystem::path> candidates, base::TerminalSize parent_size);

  void write_candidates();
  void ingest_focus_notifications(std::string_view bytes);
  void read_selection();
  [[nodiscard]] static base::TerminalSize picker_size_for(base::TerminalSize parent_size);

  std::unique_ptr<PathPickerProcess> process;
  std::vector<std::filesystem::path> candidate_paths;
  base::TerminalSize parent_terminal_size;
  base::TerminalSize picker_terminal_size;
  std::unique_ptr<TerminalScreen> terminal_screen;
  std::optional<std::filesystem::path> selection;
  std::optional<std::size_t> selection_index;
  std::optional<std::size_t> highlighted_candidate_index;
  std::string pending_focus_notification_bytes;
};

}  // namespace moe::parent
