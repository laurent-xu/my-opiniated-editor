#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"
#include "src/base/process_id.h"
#include "src/base/terminal_size.h"

namespace moe::bridge {

class ParentPtySession {
 public:
  static std::unique_ptr<ParentPtySession> start(std::vector<std::string> const& command,
                                                 std::filesystem::path const& working_directory,
                                                 base::TerminalSize size);

  ParentPtySession(ParentPtySession const&) = delete;
  ParentPtySession& operator=(ParentPtySession const&) = delete;
  ParentPtySession(ParentPtySession&& other) noexcept;
  ParentPtySession& operator=(ParentPtySession&& other) noexcept;
  ~ParentPtySession();

  [[nodiscard]] base::ProcessId child_pid() const;
  [[nodiscard]] base::FileDescriptor file_descriptor() const;
  [[nodiscard]] base::FileDescriptor status_file_descriptor() const;
  [[nodiscard]] base::FileDescriptor view_file_descriptor() const;
  void write(std::string_view bytes) const;
  void write_view(std::string_view bytes) const;
  [[nodiscard]] std::string read_until(std::string_view needle,
                                       std::chrono::milliseconds timeout) const;
  void resize(base::TerminalSize size) const;

 private:
  struct Handles {
    base::OwnedFileDescriptor master_fd;
    base::OwnedFileDescriptor status_fd;
    base::OwnedFileDescriptor view_fd;
    base::ProcessId child_pid;
  };

  explicit ParentPtySession(Handles handles);

  void reset() noexcept;

  base::OwnedFileDescriptor master_file_descriptor;
  base::OwnedFileDescriptor status_descriptor;
  base::OwnedFileDescriptor view_descriptor;
  base::ProcessId child_process_id;
};

}  // namespace moe::bridge
