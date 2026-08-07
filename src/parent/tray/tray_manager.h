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
#include "src/base/terminal_size.h"
#include "src/parent/pane/pane_layout.h"
#include "src/parent/pane/pane_move_session.h"
#include "src/parent/pane/pane_navigation.h"
#include "src/parent/pane/pane_selection.h"
#include "src/parent/tray/tray_config.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_number.h"
#include "src/parent/tray/tray_output_source.h"
#include "src/parent/tray/tray_snapshot.h"

namespace moe::parent {

class Tray;
class WorktreeManagementOverlay;

class TrayManager {
 public:
  static std::unique_ptr<TrayManager> start(TrayConfig config);

  TrayManager(TrayManager const&) = delete;
  TrayManager& operator=(TrayManager const&) = delete;
  ~TrayManager();

  void write_input(std::string_view bytes);
  [[nodiscard]] std::optional<std::string> read_active_output();
  [[nodiscard]] std::optional<std::string> read_output(TrayId const& id);
  [[nodiscard]] std::optional<std::string> read_output(TrayId const& id, PaneId pane_id);
  [[nodiscard]] std::string active_redraw_output() const;
  void resize_active(base::TerminalSize size);
  [[nodiscard]] PaneId split_active_focused_pane(PaneSplitAxis axis, PaneInsertion insertion);
  [[nodiscard]] bool focus_active_pane(PaneId pane_id);
  [[nodiscard]] bool focus_active_pane_direction(PaneFocusDirection direction);
  [[nodiscard]] bool close_active_focused_pane();
  [[nodiscard]] bool toggle_active_focused_pane_maximized();
  [[nodiscard]] bool active_focused_pane_is_maximized() const;
  [[nodiscard]] bool toggle_active_pane_selection();
  [[nodiscard]] bool step_active_pane_selection(PaneFocusDirection direction);
  [[nodiscard]] bool promote_active_pane_selection();
  [[nodiscard]] bool descend_active_pane_selection();
  [[nodiscard]] bool resize_active_pane_selection(int delta_percentage);
  [[nodiscard]] bool equalize_active_pane_selection();
  [[nodiscard]] bool rotate_active_pane_level();
  [[nodiscard]] bool toggle_active_pane_move();
  [[nodiscard]] bool step_active_pane_move_target(PaneFocusDirection direction);
  [[nodiscard]] bool promote_active_pane_move_target();
  [[nodiscard]] bool descend_active_pane_move_target();
  [[nodiscard]] bool toggle_active_pane_move_swap();
  [[nodiscard]] bool set_active_pane_move_drop_direction(PaneDropDirection direction);
  [[nodiscard]] bool advance_active_pane_move();
  [[nodiscard]] std::optional<PaneMoveSession> const& active_pane_move_session() const;
  [[nodiscard]] std::optional<PaneSelection> const& active_pane_selection() const;
  [[nodiscard]] PaneLayout const& active_pane_layout() const;
  [[nodiscard]] PaneId active_focused_pane_id() const;
  [[nodiscard]] TraySnapshot switch_to(TrayNumber number);
  [[nodiscard]] TraySnapshot switch_to_worktree(std::filesystem::path const& path);
  [[nodiscard]] bool destroy_tray(TrayId const& id);
  [[nodiscard]] bool destroy_exited_trays();
  [[nodiscard]] base::FileDescriptor active_content_file_descriptor() const;
  [[nodiscard]] TrayId active_id() const;
  [[nodiscard]] TraySnapshot active_snapshot() const;
  [[nodiscard]] std::vector<TraySnapshot> tray_snapshots() const;
  [[nodiscard]] std::vector<TrayPaneOutputSource> output_sources() const;
  void set_active_worktree_management_overlay(std::unique_ptr<WorktreeManagementOverlay> overlay);
  void clear_active_worktree_management_overlay();
  void clear_worktree_management_overlay(TrayId const& id);
  [[nodiscard]] WorktreeManagementOverlay* active_worktree_management_overlay();
  [[nodiscard]] WorktreeManagementOverlay const* active_worktree_management_overlay() const;
  [[nodiscard]] WorktreeManagementOverlay* worktree_management_overlay(TrayId const& id);
  [[nodiscard]] std::vector<TrayOutputSource> worktree_management_overlay_output_sources() const;
  [[nodiscard]] std::string active_worktree_management_overlay_redraw_output() const;
  [[nodiscard]] bool active_worktree_management_overlay_previews(TrayId const& id) const;

 private:
  explicit TrayManager(TrayConfig config);

  [[nodiscard]] Tray const& active_tray() const;
  [[nodiscard]] Tray& mutable_active_tray();
  [[nodiscard]] Tray const& tray(TrayId const& id) const;
  [[nodiscard]] Tray const* find_tray(TrayId const& id) const;
  [[nodiscard]] Tray& mutable_tray(TrayId const& id);
  [[nodiscard]] Tray& ensure_tray(TrayNumber number);
  [[nodiscard]] Tray& ensure_worktree_tray(std::filesystem::path const& root);
  [[nodiscard]] Tray const& worktree_tray(std::filesystem::path const& root) const;
  [[nodiscard]] Tray& mutable_worktree_tray(std::filesystem::path const& root);
  void refresh_active_overlay_session_trays();
  void activate_anonymous_tray_one();
  [[nodiscard]] static std::size_t tray_index(TrayNumber number);

  TrayConfig config;
  base::TerminalSize current_size;
  TrayId active_tray_id;
  std::array<std::unique_ptr<Tray>, TrayNumber::MAX_VALUE> anonymous_trays;
  std::map<std::filesystem::path, std::unique_ptr<Tray>> worktree_trays;
};

}  // namespace moe::parent
