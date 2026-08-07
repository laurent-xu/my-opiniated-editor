#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/base/terminal_size.h"
#include "src/parent/pane/pane_id.h"
#include "src/parent/pane/pane_layout.h"
#include "src/parent/pane/pane_navigation.h"
#include "src/parent/pane/pane_selection.h"
#include "src/parent/terminal/screen/terminal_position.h"
#include "src/parent/tray/tray_config.h"
#include "src/parent/tray/tray_id.h"
#include "src/parent/tray/tray_output_source.h"
#include "src/parent/tray/tray_snapshot.h"
#include "src/process/process_exit_status.h"

namespace moe::parent {

class Pane;
class WorktreeManagementOverlay;

struct TrayExitUpdate {
  bool changed;
  std::optional<process::ProcessExitStatus> tray_exit_status;
};

class Tray {
 public:
  static std::unique_ptr<Tray> start(TrayId id, TrayConfig const& config);

  Tray(Tray const&) = delete;
  Tray& operator=(Tray const&) = delete;
  ~Tray();

  void write_input(std::string_view bytes) const;
  [[nodiscard]] std::optional<std::string> read_output();
  [[nodiscard]] std::optional<std::string> read_output(PaneId pane_id);
  [[nodiscard]] std::string redraw_output() const;
  [[nodiscard]] std::string preview_output(TerminalPosition origin,
                                           base::TerminalSize region_size) const;
  void resize(base::TerminalSize size);
  [[nodiscard]] TrayExitUpdate reap_exited_panes();
  [[nodiscard]] base::FileDescriptor file_descriptor() const;
  [[nodiscard]] std::vector<TrayPaneOutputSource> output_sources() const;
  [[nodiscard]] PaneId focused_pane_id() const;
  [[nodiscard]] PaneId split_focused_pane(PaneSplitAxis axis, PaneInsertion insertion);
  [[nodiscard]] bool focus_pane(PaneId pane_id);
  [[nodiscard]] bool focus_pane_direction(PaneFocusDirection direction);
  [[nodiscard]] bool close_focused_pane();
  [[nodiscard]] bool toggle_focused_pane_maximized();
  [[nodiscard]] bool focused_pane_is_maximized() const;
  [[nodiscard]] bool toggle_pane_selection();
  [[nodiscard]] bool step_pane_selection(PaneFocusDirection direction);
  [[nodiscard]] bool promote_pane_selection();
  [[nodiscard]] bool descend_pane_selection();
  [[nodiscard]] bool resize_selected_panes(int delta_percentage);
  [[nodiscard]] bool equalize_selected_panes();
  [[nodiscard]] std::optional<PaneSelection> const& selection() const;
  [[nodiscard]] PaneLayout const& layout() const;
  [[nodiscard]] TrayId const& id() const;
  [[nodiscard]] TraySnapshot snapshot() const;
  void set_worktree_management_overlay(std::unique_ptr<WorktreeManagementOverlay> overlay);
  void clear_worktree_management_overlay();
  [[nodiscard]] WorktreeManagementOverlay* worktree_management_overlay() noexcept;
  [[nodiscard]] WorktreeManagementOverlay const* worktree_management_overlay() const noexcept;

 private:
  Tray(TrayId id, PaneConfig config, PaneId initial_pane_id, std::unique_ptr<Pane> initial_pane);

  [[nodiscard]] Pane const& pane(PaneId pane_id) const;
  [[nodiscard]] Pane& mutable_pane(PaneId pane_id);
  [[nodiscard]] PaneId allocate_pane_id();
  [[nodiscard]] PaneSelection selection_or_focused_pane() const;
  [[nodiscard]] std::string render_layout(TerminalPosition origin, base::TerminalSize size,
                                          bool restore_focused_cursor) const;
  void resize_panes();

  TrayId tray_id;
  std::string tray_label;
  std::filesystem::path cwd;
  PaneConfig pane_config;
  base::TerminalSize current_size;
  PaneLayout pane_layout;
  PaneId focused_pane;
  PaneId::Value next_pane_value{2};
  std::map<PaneId, std::unique_ptr<Pane>> panes;
  bool pane_maximized{false};
  std::optional<PaneSelection> pane_selection;
  std::unique_ptr<WorktreeManagementOverlay> worktree_overlay;
};

}  // namespace moe::parent
