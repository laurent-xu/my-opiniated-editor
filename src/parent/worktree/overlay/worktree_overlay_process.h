#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/base/terminal_size.h"
#include "src/process/process_exit_status.h"

namespace moe::parent {

class ContentPtySession;

class WorktreeOverlayProcess {
 public:
  WorktreeOverlayProcess();
  WorktreeOverlayProcess(WorktreeOverlayProcess const&) = delete;
  WorktreeOverlayProcess& operator=(WorktreeOverlayProcess const&) = delete;
  ~WorktreeOverlayProcess();

  void start(std::vector<std::string> const& command,
             std::filesystem::path const& working_directory, base::TerminalSize size);
  void clear();
  void write(std::string_view bytes) const;
  [[nodiscard]] bool read_process_output();
  [[nodiscard]] bool refresh_process_state();
  void resize(base::TerminalSize size) const;

  [[nodiscard]] std::optional<base::FileDescriptor> file_descriptor() const;
  [[nodiscard]] std::vector<std::string> const& transcript_lines() const noexcept;
  [[nodiscard]] std::string const& transcript_line() const noexcept;
  [[nodiscard]] bool result_succeeded() const noexcept;

 private:
  void append_process_output(std::string_view bytes);
  void append_transcript_line();

  std::unique_ptr<ContentPtySession> process;
  std::vector<std::string> lines;
  std::string current_line;
  std::optional<process::ProcessExitStatus> result;
  bool escape_sequence = false;
  bool control_sequence = false;
};

}  // namespace moe::parent
