#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"
#include "src/base/process_id.h"
#include "src/base/terminal_size.h"
#include "src/process/process_exit_status.h"

namespace moe::parent {

class ContentPtySession {
 public:
  static std::unique_ptr<ContentPtySession> start(std::vector<std::string> const& command,
                                                  std::filesystem::path const& working_directory,
                                                  base::TerminalSize size);

  ContentPtySession(ContentPtySession const&) = delete;
  ContentPtySession& operator=(ContentPtySession const&) = delete;
  ContentPtySession(ContentPtySession&& other) noexcept;
  ContentPtySession& operator=(ContentPtySession&& other) noexcept;
  ~ContentPtySession();

  [[nodiscard]] base::ProcessId child_pid() const;
  [[nodiscard]] base::FileDescriptor file_descriptor() const;
  void write(std::string_view bytes) const;
  [[nodiscard]] std::optional<std::string> read_available() const;
  void resize(base::TerminalSize size) const;
  [[nodiscard]] std::optional<process::ProcessExitStatus> try_wait_for_exit() noexcept;

 private:
  struct Handles {
    base::OwnedFileDescriptor master_fd;
    base::ProcessId child_pid;
  };

  explicit ContentPtySession(Handles handles);

  void reset() noexcept;

  base::OwnedFileDescriptor master_file_descriptor;
  base::ProcessId child_process_id;
};

}  // namespace moe::parent
