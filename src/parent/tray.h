#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/parent/content_pty_session.h"
#include "src/parent/tray_config.h"
#include "src/parent/tray_id.h"
#include "src/parent/tray_snapshot.h"

namespace moe::parent {

class Tray {
 public:
  static std::unique_ptr<Tray> start(TrayId id, TrayConfig const& config);

  Tray(Tray const&) = delete;
  Tray& operator=(Tray const&) = delete;
  ~Tray();

  void write_input(std::string_view bytes) const;
  [[nodiscard]] std::optional<std::string> read_output() const;
  void resize(TerminalSize size) const;
  [[nodiscard]] std::optional<int> try_wait_for_exit() noexcept;
  [[nodiscard]] base::FileDescriptor file_descriptor() const;
  [[nodiscard]] TraySnapshot snapshot() const;

 private:
  Tray(TrayId id, std::filesystem::path working_directory,
       std::unique_ptr<ContentPtySession> content);

  TrayId tray_id;
  std::string tray_label;
  std::filesystem::path cwd;
  std::unique_ptr<ContentPtySession> content;
};

}  // namespace moe::parent
