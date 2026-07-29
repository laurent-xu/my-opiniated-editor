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
#include "src/parent/content_pty_session.h"
#include "src/parent/overlay.h"

namespace moe::parent {

class WorktreeManagementOverlay : public Overlay {
 public:
  static std::unique_ptr<WorktreeManagementOverlay> start(std::filesystem::path parent_executable,
                                                          std::filesystem::path registry_path,
                                                          std::filesystem::path working_directory,
                                                          TerminalSize size);

  WorktreeManagementOverlay(WorktreeManagementOverlay const&) = delete;
  WorktreeManagementOverlay& operator=(WorktreeManagementOverlay const&) = delete;
  ~WorktreeManagementOverlay() override;

  void write_input(std::string_view bytes) override;
  [[nodiscard]] bool read_process_output() override;
  [[nodiscard]] bool refresh_process_state() override;
  void resize(TerminalSize size) override;

  [[nodiscard]] std::optional<base::FileDescriptor> process_file_descriptor() const override;
  [[nodiscard]] std::string redraw_output() const override;

 private:
  enum class Stage : std::uint8_t {
    REPOSITORY_ROOT,
    CLONE_URL,
    RUNNING,
    RESULT,
  };

  enum class InputSequenceState : std::uint8_t {
    NORMAL,
    ESCAPE,
    CONTROL_SEQUENCE,
  };

  WorktreeManagementOverlay(std::filesystem::path parent_executable,
                            std::filesystem::path registry_path,
                            std::filesystem::path working_directory, TerminalSize size);

  void write_editing_input(unsigned char byte);
  void handle_input_control_sequence(unsigned char final_byte);
  void move_input_cursor_left();
  void move_input_cursor_right();
  void erase_before_input_cursor();
  void erase_at_input_cursor();
  void clear_input();
  void submit_input();
  void submit_repository_root();
  void submit_clone_url();
  void start_registration(std::optional<std::string> clone_url);
  void append_process_output(std::string_view bytes);
  void append_transcript_line();
  [[nodiscard]] std::filesystem::path resolved_repository_root() const;

  std::filesystem::path parent_executable;
  std::filesystem::path registry_path;
  std::filesystem::path working_directory;
  TerminalSize size;
  Stage stage = Stage::REPOSITORY_ROOT;
  std::string input;
  std::size_t input_cursor_offset = 0;
  InputSequenceState input_sequence_state = InputSequenceState::NORMAL;
  std::string input_control_sequence_parameters;
  std::string error_message;
  std::optional<std::filesystem::path> repository_root;
  std::unique_ptr<ContentPtySession> process;
  std::vector<std::string> transcript_lines;
  std::string transcript_line;
  bool process_escape_sequence = false;
  bool process_control_sequence = false;
  bool result_succeeded = false;
};

}  // namespace moe::parent
