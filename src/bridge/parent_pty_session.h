#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/base/process_id.h"
#include "src/bridge/pty_size.h"

namespace moe::bridge {

class ParentPtySession {
 public:
  static std::unique_ptr<ParentPtySession> start(std::vector<std::string> const& command,
                                                 std::filesystem::path const& working_directory,
                                                 PtySize size);

  ParentPtySession(ParentPtySession const&) = delete;
  ParentPtySession& operator=(ParentPtySession const&) = delete;
  ParentPtySession(ParentPtySession&& other) noexcept;
  ParentPtySession& operator=(ParentPtySession&& other) noexcept;
  ~ParentPtySession();

  [[nodiscard]] base::ProcessId child_pid() const;
  [[nodiscard]] base::FileDescriptor file_descriptor() const;
  void write(std::string_view bytes) const;
  [[nodiscard]] std::string read_until(std::string_view needle,
                                       std::chrono::milliseconds timeout) const;
  void resize(PtySize size) const;

 private:
  struct Handles {
    base::FileDescriptor master_fd;
    base::ProcessId child_pid;
  };

  explicit ParentPtySession(Handles handles);

  void reset() noexcept;

  base::FileDescriptor master_file_descriptor;
  base::ProcessId child_process_id;
};

}  // namespace moe::bridge
