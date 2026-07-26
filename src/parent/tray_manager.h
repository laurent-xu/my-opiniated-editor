#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/parent/tray.h"

namespace moe::parent {

class TrayManager {
 public:
  static std::unique_ptr<TrayManager> start(TrayConfig config);

  TrayManager(TrayManager const&) = delete;
  TrayManager& operator=(TrayManager const&) = delete;
  ~TrayManager();

  void write_input(std::string_view bytes);
  [[nodiscard]] std::optional<std::string> read_active_output() const;
  void resize_active(TerminalSize size) const;
  [[nodiscard]] std::optional<int> try_wait_for_active_exit() noexcept;
  [[nodiscard]] base::FileDescriptor active_content_file_descriptor() const;
  [[nodiscard]] TraySnapshot active_snapshot() const;

 private:
  explicit TrayManager(std::unique_ptr<Tray> active_tray);

  std::unique_ptr<Tray> active_tray;
};

}  // namespace moe::parent
