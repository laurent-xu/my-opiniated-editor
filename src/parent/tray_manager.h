#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
  void resize_active(TerminalSize size);
  [[nodiscard]] TraySnapshot switch_to(TrayNumber number);
  [[nodiscard]] std::optional<int> try_wait_for_active_exit() noexcept;
  [[nodiscard]] base::FileDescriptor active_content_file_descriptor() const;
  [[nodiscard]] TraySnapshot active_snapshot() const;
  [[nodiscard]] std::vector<TraySnapshot> tray_snapshots() const;

 private:
  explicit TrayManager(TrayConfig config);

  [[nodiscard]] Tray const& active_tray() const;
  [[nodiscard]] Tray& mutable_active_tray();
  [[nodiscard]] Tray& ensure_tray(TrayNumber number);
  [[nodiscard]] static std::size_t tray_index(TrayNumber number);

  TrayConfig config;
  TerminalSize current_size;
  TrayNumber active_tray_number;
  std::array<std::unique_ptr<Tray>, TrayNumber::MAX_VALUE> anonymous_trays;
};

}  // namespace moe::parent
