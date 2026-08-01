#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"
#include "src/base/process_id.h"
#include "src/base/terminal_size.h"
#include "src/process/process_exit_status.h"

namespace moe::parent {

class PathPickerProcess {
 public:
  [[nodiscard]] static std::unique_ptr<PathPickerProcess> start(std::string fzf_executable,
                                                                std::string prompt,
                                                                base::TerminalSize size);

  PathPickerProcess(PathPickerProcess const&) = delete;
  PathPickerProcess& operator=(PathPickerProcess const&) = delete;
  ~PathPickerProcess();

  void write(std::string_view bytes) const;
  void write_candidate_input(std::string_view bytes) const;
  void close_candidate_input();
  [[nodiscard]] std::optional<std::string> read_available() const;
  [[nodiscard]] bool refresh_process_state();
  [[nodiscard]] std::string read_result() const;
  void resize(base::TerminalSize size) const;

  [[nodiscard]] std::optional<base::FileDescriptor> file_descriptor() const;
  [[nodiscard]] bool finished() const noexcept;
  [[nodiscard]] bool result_succeeded() const noexcept;

 private:
  PathPickerProcess(base::OwnedFileDescriptor terminal_master,
                    base::OwnedFileDescriptor candidate_input,
                    base::OwnedFileDescriptor result_output, base::ProcessId child_pid);

  void reset() noexcept;

  base::OwnedFileDescriptor terminal_master;
  base::OwnedFileDescriptor candidate_input;
  base::OwnedFileDescriptor result_output;
  base::ProcessId child_process_id;
  std::optional<process::ProcessExitStatus> exit_status;
  bool process_finished = false;
};

}  // namespace moe::parent
