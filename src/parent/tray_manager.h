#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/parent/tray.h"
#include "src/parent/tray_output_source.h"

namespace moe::parent {

class TrayManager {
 public:
  static std::unique_ptr<TrayManager> start(TrayConfig config);

  TrayManager(TrayManager const&) = delete;
  TrayManager& operator=(TrayManager const&) = delete;
  ~TrayManager();

  void write_input(std::string_view bytes);
  [[nodiscard]] std::optional<std::string> read_active_output();
  [[nodiscard]] std::optional<std::string> read_output(TrayId const& id);
  [[nodiscard]] std::string active_redraw_output() const;
  void resize_active(TerminalSize size);
  [[nodiscard]] TraySnapshot switch_to(TrayNumber number);
  [[nodiscard]] TraySnapshot switch_to_worktree(std::filesystem::path const& path);
  [[nodiscard]] std::optional<int> try_wait_for_active_exit() noexcept;
  [[nodiscard]] base::FileDescriptor active_content_file_descriptor() const;
  [[nodiscard]] TrayId active_id() const;
  [[nodiscard]] TraySnapshot active_snapshot() const;
  [[nodiscard]] std::vector<TraySnapshot> tray_snapshots() const;
  [[nodiscard]] std::vector<TrayOutputSource> output_sources() const;

 private:
  explicit TrayManager(TrayConfig config);

  [[nodiscard]] Tray const& active_tray() const;
  [[nodiscard]] Tray& mutable_active_tray();
  [[nodiscard]] Tray const& tray(TrayId const& id) const;
  [[nodiscard]] Tray& mutable_tray(TrayId const& id);
  [[nodiscard]] Tray& ensure_tray(TrayNumber number);
  [[nodiscard]] Tray& ensure_worktree_tray(std::filesystem::path const& root);
  [[nodiscard]] Tray const& worktree_tray(std::filesystem::path const& root) const;
  [[nodiscard]] Tray& mutable_worktree_tray(std::filesystem::path const& root);
  [[nodiscard]] static std::size_t tray_index(TrayNumber number);

  TrayConfig config;
  TerminalSize current_size;
  TrayId active_tray_id;
  std::array<std::unique_ptr<Tray>, TrayNumber::MAX_VALUE> anonymous_trays;
  std::map<std::filesystem::path, std::unique_ptr<Tray>> worktree_trays;
};

}  // namespace moe::parent
