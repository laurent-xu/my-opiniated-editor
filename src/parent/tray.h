#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "src/base/file_descriptor.h"
#include "src/base/terminal_size.h"
#include "src/parent/terminal_screen.h"
#include "src/parent/tray_config.h"
#include "src/parent/tray_id.h"
#include "src/parent/tray_snapshot.h"

namespace moe::parent {

class ContentPtySession;
class WorktreeManagementOverlay;

class Tray {
 public:
  static std::unique_ptr<Tray> start(TrayId id, TrayConfig const& config);

  Tray(Tray const&) = delete;
  Tray& operator=(Tray const&) = delete;
  ~Tray();

  void write_input(std::string_view bytes) const;
  [[nodiscard]] std::optional<std::string> read_output();
  [[nodiscard]] std::string redraw_output() const;
  [[nodiscard]] std::string preview_output(TerminalPosition origin,
                                           base::TerminalSize region_size) const;
  void resize(base::TerminalSize size);
  [[nodiscard]] std::optional<int> try_wait_for_exit() noexcept;
  [[nodiscard]] base::FileDescriptor file_descriptor() const;
  [[nodiscard]] TrayId const& id() const;
  [[nodiscard]] TraySnapshot snapshot() const;
  void set_worktree_management_overlay(std::unique_ptr<WorktreeManagementOverlay> overlay);
  void clear_worktree_management_overlay();
  [[nodiscard]] WorktreeManagementOverlay* worktree_management_overlay() noexcept;
  [[nodiscard]] WorktreeManagementOverlay const* worktree_management_overlay() const noexcept;

 private:
  Tray(TrayId id, std::filesystem::path working_directory,
       std::unique_ptr<ContentPtySession> content, base::TerminalSize size);

  TrayId tray_id;
  std::string tray_label;
  std::filesystem::path cwd;
  std::unique_ptr<ContentPtySession> content;
  TerminalScreen terminal_screen;
  std::unique_ptr<WorktreeManagementOverlay> worktree_overlay;
};

}  // namespace moe::parent
