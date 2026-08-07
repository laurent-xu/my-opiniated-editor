#include "src/parent/tray/tray.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "src/parent/pane/pane.h"
#include "src/parent/pane/pane_geometry.h"
#include "src/parent/pane/pane_resize.h"
#include "src/parent/pane/pane_rotate.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"

namespace moe::parent {

namespace {

PaneId required_pane_id(PaneId::Value const value) {
  std::optional<PaneId> const pane_id = PaneId::from_value(value);
  if (!pane_id.has_value()) {
    throw std::logic_error("pane id cannot be zero");
  }
  return pane_id.value();
}

PaneNodeId required_pane_node(std::optional<PaneNodeId> const node_id) {
  if (!node_id.has_value()) {
    throw std::logic_error("pane is missing from its tray layout");
  }
  return node_id.value();
}

std::string render_separators(std::vector<PaneSeparator> const& separators) {
  std::string output("\x1b[?25l\x1b[?7l");
  for (PaneSeparator const& separator : separators) {
    int const rows = std::max(separator.region.size.rows, 0);
    int const columns = std::max(separator.region.size.cols, 0);
    std::string const fill(static_cast<std::size_t>(columns), ' ');
    for (int row = 0; row < rows; ++row) {
      output.append("\x1b[");
      output.append(std::to_string(separator.region.origin.row + row + 1));
      output.push_back(';');
      output.append(std::to_string(separator.region.origin.column + 1));
      output.append("H\x1b[0;48;5;240m");
      output.append(fill);
    }
  }
  output.append("\x1b[0m\x1b[?7h\x1b[?25l");
  return output;
}

struct ColoredCells {
  int row;
  int column;
  int count;
  int color;
};

void append_colored_cells(std::string& output, ColoredCells const cells) {
  if (cells.count <= 0) {
    return;
  }
  output.append("\x1b[");
  output.append(std::to_string(cells.row + 1));
  output.push_back(';');
  output.append(std::to_string(cells.column + 1));
  output.append("H\x1b[0;48;5;");
  output.append(std::to_string(cells.color));
  output.push_back('m');
  output.append(static_cast<std::size_t>(cells.count), ' ');
}

void append_selection_border(std::string& output, PaneRegion const& region, int const color) {
  int const rows = std::max(region.size.rows, 0);
  int const columns = std::max(region.size.cols, 0);
  if (rows == 0 || columns == 0) {
    return;
  }

  append_colored_cells(
      output,
      {.row = region.origin.row, .column = region.origin.column, .count = columns, .color = color});
  if (rows > 1) {
    append_colored_cells(output, {.row = region.origin.row + rows - 1,
                                  .column = region.origin.column,
                                  .count = columns,
                                  .color = color});
  }
  for (int row = 1; row + 1 < rows; ++row) {
    append_colored_cells(output, {.row = region.origin.row + row,
                                  .column = region.origin.column,
                                  .count = 1,
                                  .color = color});
    if (columns > 1) {
      append_colored_cells(output, {.row = region.origin.row + row,
                                    .column = region.origin.column + columns - 1,
                                    .count = 1,
                                    .color = color});
    }
  }
}

std::string render_selection(PaneGeometry const& geometry, PaneSelection const& selection) {
  constexpr int SELECTED_COLOR = 25;
  constexpr int ACTIVE_COLOR = 33;
  std::string output("\x1b[?25l\x1b[?7l");
  for (PaneNodeId const node_id : selection.nodes()) {
    if (node_id != selection.active()) {
      append_selection_border(output, geometry.region(node_id), SELECTED_COLOR);
    }
  }
  append_selection_border(output, geometry.region(selection.active()), ACTIVE_COLOR);
  output.append("\x1b[0m\x1b[?7h\x1b[?25l");
  return output;
}

void append_leaf_pane_ids(PaneLayout const& layout, PaneNodeId const node_id,
                          std::vector<PaneId>& output) {
  PaneLayoutNode const& node = layout.node(node_id);
  if (node.is_leaf()) {
    output.push_back(node.pane_id());
    return;
  }
  for (PaneSplitChild const& child : node.split().children) {
    append_leaf_pane_ids(layout, child.node_id, output);
  }
}

void append_pane_id_borders(std::string& output, PaneLayout const& layout,
                            PaneGeometry const& geometry, std::vector<PaneId> const& pane_ids,
                            int const color) {
  for (PaneId const pane_id : pane_ids) {
    std::optional<PaneNodeId> const node_id = layout.find_pane(pane_id);
    if (node_id.has_value()) {
      append_selection_border(output, geometry.region(node_id.value()), color);
    }
  }
}

struct MoveRenderLayouts {
  PaneLayout const& original;
  PaneLayout const& rendered;
};

std::string render_move_session(MoveRenderLayouts const layouts, PaneGeometry const& geometry,
                                PaneMoveSession const& session, bool const preview_active) {
  constexpr int SOURCE_COLOR = 166;
  constexpr int PREVIEW_SOURCE_COLOR = 34;
  constexpr int TARGET_COLOR = 31;
  std::string output("\x1b[?25l\x1b[?7l");
  if (!preview_active) {
    for (PaneNodeId const source : session.source().nodes()) {
      append_selection_border(output, geometry.region(source), SOURCE_COLOR);
    }
    std::optional<PaneNodeId> const target = session.target();
    if (target.has_value()) {
      append_selection_border(output, geometry.region(target.value()), TARGET_COLOR);
    }
  } else {
    std::vector<PaneId> source_panes;
    for (PaneNodeId const source : session.source().nodes()) {
      append_leaf_pane_ids(layouts.original, source, source_panes);
    }
    append_pane_id_borders(output, layouts.rendered, geometry, source_panes, PREVIEW_SOURCE_COLOR);
    std::optional<PaneNodeId> const target = session.target();
    if (target.has_value()) {
      std::vector<PaneId> target_panes;
      append_leaf_pane_ids(layouts.original, target.value(), target_panes);
      append_pane_id_borders(output, layouts.rendered, geometry, target_panes, TARGET_COLOR);
    }
  }
  output.append("\x1b[0m\x1b[?7h\x1b[?25l");
  return output;
}

base::TerminalSize pty_size(PaneRegion const& region) {
  return {
      .rows = std::max(region.size.rows, 1),
      .cols = std::max(region.size.cols, 1),
  };
}

}  // namespace

std::unique_ptr<Tray> Tray::start(TrayId id, TrayConfig const& config) {
  PaneId const initial_pane_id = required_pane_id(1);
  std::unique_ptr<Pane> initial_pane = Pane::start(config);
  return std::unique_ptr<Tray>(
      new Tray(std::move(id), config, initial_pane_id, std::move(initial_pane)));
}

Tray::Tray(TrayId id, PaneConfig config, PaneId const initial_pane_id,
           std::unique_ptr<Pane> initial_pane)
    : tray_id(std::move(id)),
      tray_label(tray_id.label()),
      cwd(config.working_directory),
      pane_config(std::move(config)),
      current_size(pane_config.initial_size),
      pane_layout(PaneLayout::single(initial_pane_id)),
      focused_pane(initial_pane_id) {
  if (initial_pane == nullptr) {
    throw std::invalid_argument("tray requires an initial pane");
  }
  panes.emplace(initial_pane_id, std::move(initial_pane));
}

Tray::~Tray() = default;

void Tray::write_input(std::string_view const bytes) const {
  pane(focused_pane).write_input(bytes);
}

std::optional<std::string> Tray::read_output() { return mutable_pane(focused_pane).read_output(); }

std::optional<std::string> Tray::read_output(PaneId const pane_id) {
  return mutable_pane(pane_id).read_output();
}

std::string Tray::redraw_output() const {
  return render_layout({.row = 0, .column = 0}, current_size, true);
}

std::string Tray::preview_output(TerminalPosition const origin,
                                 base::TerminalSize const region_size) const {
  return render_layout(origin, region_size, false);
}

void Tray::resize(base::TerminalSize const size) {
  current_size = size;
  resize_panes();
  if (worktree_overlay != nullptr) {
    worktree_overlay->resize(size);
  }
}

TrayExitUpdate Tray::reap_exited_panes() {
  std::vector<std::pair<PaneId, process::ProcessExitStatus>> exited;
  for (auto const& [pane_id, pane] : panes) {
    std::optional<process::ProcessExitStatus> const status = pane->try_wait_for_exit();
    if (status.has_value()) {
      exited.emplace_back(pane_id, status.value());
    }
  }
  if (exited.empty()) {
    return {.changed = false, .tray_exit_status = std::nullopt};
  }
  if (exited.size() == panes.size()) {
    return {.changed = true, .tray_exit_status = exited.front().second};
  }

  for (auto const& [pane_id, status] : exited) {
    static_cast<void>(status);
    PaneNodeId const node_id = required_pane_node(pane_layout.find_pane(pane_id));
    std::optional<PaneNodeId> const next_focus = pane_layout.remove_leaf(node_id);
    if (!next_focus.has_value()) {
      throw std::logic_error("cannot remove the last live pane from a tray");
    }
    if (pane_id == focused_pane) {
      focused_pane = pane_layout.node(next_focus.value()).pane_id();
    }
    panes.erase(pane_id);
  }
  if (!panes.contains(focused_pane)) {
    focused_pane = pane_layout.node(pane_layout.leaf_nodes().front()).pane_id();
  }
  pane_maximized = false;
  pane_selection.reset();
  pane_move_session.reset();
  resize_panes();
  return {.changed = true, .tray_exit_status = std::nullopt};
}

base::FileDescriptor Tray::file_descriptor() const { return pane(focused_pane).file_descriptor(); }

std::vector<TrayPaneOutputSource> Tray::output_sources() const {
  std::vector<TrayPaneOutputSource> sources;
  sources.reserve(panes.size());
  for (auto const& [pane_id, pane] : panes) {
    sources.push_back(TrayPaneOutputSource{
        .tray_id = tray_id,
        .pane_id = pane_id,
        .file_descriptor = pane->file_descriptor(),
    });
  }
  return sources;
}

PaneId Tray::focused_pane_id() const { return focused_pane; }

PaneId Tray::split_focused_pane(PaneSplitAxis const axis, PaneInsertion const insertion) {
  PaneId const new_pane_id = allocate_pane_id();
  PaneConfig config = pane_config;
  config.initial_size = current_size;
  std::unique_ptr<Pane> new_pane = Pane::start(config);
  PaneNodeId const focused_node = required_pane_node(pane_layout.find_pane(focused_pane));
  static_cast<void>(pane_layout.split_leaf(focused_node, axis, new_pane_id, insertion));
  panes.emplace(new_pane_id, std::move(new_pane));
  focused_pane = new_pane_id;
  pane_maximized = false;
  pane_selection.reset();
  pane_move_session.reset();
  resize_panes();
  return new_pane_id;
}

bool Tray::focus_pane(PaneId const pane_id) {
  if (!panes.contains(pane_id)) {
    return false;
  }
  bool const changed = pane_id != focused_pane;
  focused_pane = pane_id;
  if (changed) {
    pane_selection.reset();
    pane_move_session.reset();
  }
  if (pane_maximized && changed) {
    resize_panes();
  }
  return true;
}

bool Tray::focus_pane_direction(PaneFocusDirection const direction) {
  PaneGeometry const geometry = calculate_pane_geometry(
      pane_layout, {.origin = {.row = 0, .column = 0}, .size = current_size});
  PaneNodeId const focused_node = required_pane_node(pane_layout.find_pane(focused_pane));
  std::optional<PaneNodeId> const target =
      find_directional_pane(pane_layout, geometry, focused_node, direction);
  if (!target.has_value()) {
    return false;
  }
  return focus_pane(pane_layout.node(target.value()).pane_id());
}

bool Tray::close_focused_pane() {
  if (panes.size() == 1) {
    return false;
  }

  PaneId const closing_pane = focused_pane;
  PaneNodeId const closing_node = required_pane_node(pane_layout.find_pane(closing_pane));
  std::optional<PaneNodeId> const next_focus = pane_layout.remove_leaf(closing_node);
  if (!next_focus.has_value()) {
    throw std::logic_error("multi-pane tray close did not choose a surviving pane");
  }
  focused_pane = pane_layout.node(next_focus.value()).pane_id();
  panes.erase(closing_pane);
  pane_maximized = false;
  pane_selection.reset();
  pane_move_session.reset();
  resize_panes();
  return true;
}

bool Tray::toggle_focused_pane_maximized() {
  if (panes.size() == 1U) {
    return false;
  }
  pane_selection.reset();
  pane_move_session.reset();
  pane_maximized = !pane_maximized;
  resize_panes();
  return true;
}

bool Tray::focused_pane_is_maximized() const { return pane_maximized; }

bool Tray::toggle_pane_selection() {
  pane_move_session.reset();
  if (pane_selection.has_value()) {
    pane_selection.reset();
    return true;
  }
  if (pane_maximized) {
    pane_maximized = false;
    resize_panes();
  }
  PaneNodeId const focused_node = required_pane_node(pane_layout.find_pane(focused_pane));
  pane_selection = PaneSelection::single(pane_layout, focused_node);
  return true;
}

bool Tray::step_pane_selection(PaneFocusDirection const direction) {
  if (!pane_selection.has_value()) {
    return false;
  }
  std::optional<PaneNodeId> const parent = pane_selection->parent();
  if (!parent.has_value()) {
    return false;
  }

  PaneSplitAxis const axis = pane_layout.node(parent.value()).split().axis;
  std::optional<PaneSiblingDirection> sibling_direction;
  if (axis == PaneSplitAxis::LEFT_TO_RIGHT) {
    if (direction == PaneFocusDirection::LEFT) {
      sibling_direction = PaneSiblingDirection::PREVIOUS;
    } else if (direction == PaneFocusDirection::RIGHT) {
      sibling_direction = PaneSiblingDirection::NEXT;
    }
  } else if (direction == PaneFocusDirection::UP) {
    sibling_direction = PaneSiblingDirection::PREVIOUS;
  } else if (direction == PaneFocusDirection::DOWN) {
    sibling_direction = PaneSiblingDirection::NEXT;
  }
  if (!sibling_direction.has_value()) {
    return false;
  }

  PaneSelection const stepped = pane_selection->step(pane_layout, sibling_direction.value());
  if (stepped.active() == pane_selection->active()) {
    return false;
  }
  pane_selection = stepped;
  return true;
}

bool Tray::promote_pane_selection() {
  if (!pane_selection.has_value()) {
    return false;
  }
  PaneSelection const promoted = pane_selection->promote(pane_layout);
  if (promoted.nodes() == pane_selection->nodes()) {
    return false;
  }
  pane_selection = promoted;
  return true;
}

bool Tray::descend_pane_selection() {
  if (!pane_selection.has_value()) {
    return false;
  }
  PaneSelection const descended = pane_selection->descend(pane_layout);
  if (descended.nodes() == pane_selection->nodes()) {
    return false;
  }
  pane_selection = descended;
  return true;
}

bool Tray::resize_selected_panes(int const delta_percentage) {
  pane_move_session.reset();
  bool const changed =
      resize_pane_selection(pane_layout, selection_or_focused_pane(), delta_percentage);
  if (changed) {
    pane_maximized = false;
    resize_panes();
  }
  return changed;
}

bool Tray::equalize_selected_panes() {
  pane_move_session.reset();
  bool const changed =
      pane_selection.has_value()
          ? equalize_pane_selection(pane_layout, pane_selection.value())
          : equalize_pane_selection_level(pane_layout, selection_or_focused_pane());
  if (changed) {
    pane_maximized = false;
    resize_panes();
  }
  return changed;
}

bool Tray::rotate_selected_pane_level() {
  pane_move_session.reset();
  PaneSelection rotated_selection = selection_or_focused_pane();
  bool const changed = rotate_pane_selection_level(pane_layout, rotated_selection);
  if (changed) {
    pane_maximized = false;
    if (pane_selection.has_value()) {
      pane_selection = std::move(rotated_selection);
    }
    resize_panes();
  }
  return changed;
}

bool Tray::toggle_pane_move() {
  if (pane_move_session.has_value()) {
    pane_move_session.reset();
    return true;
  }
  if (panes.size() == 1U) {
    return false;
  }
  PaneSelection source = selection_or_focused_pane();
  pane_selection.reset();
  if (pane_maximized) {
    pane_maximized = false;
    resize_panes();
  }
  pane_move_session = PaneMoveSession::begin(std::move(source));
  return true;
}

bool Tray::step_pane_move_target(PaneFocusDirection const direction) {
  if (!pane_move_session.has_value()) {
    return false;
  }
  PaneGeometry const geometry = calculate_pane_geometry(
      pane_layout, {.origin = {.row = 0, .column = 0}, .size = current_size});
  return pane_move_session->step_target(pane_layout, geometry, direction);
}

bool Tray::promote_pane_move_target() {
  return pane_move_session.has_value() && pane_move_session->promote_target(pane_layout);
}

bool Tray::descend_pane_move_target() {
  return pane_move_session.has_value() && pane_move_session->descend_target(pane_layout);
}

bool Tray::toggle_pane_move_swap() {
  return pane_move_session.has_value() && pane_move_session->toggle_swap();
}

bool Tray::set_pane_move_drop_direction(PaneDropDirection const direction) {
  return pane_move_session.has_value() && pane_move_session->set_drop_direction(direction);
}

bool Tray::advance_pane_move() {
  if (!pane_move_session.has_value()) {
    return false;
  }
  if (pane_move_session->operation() == PaneMoveOperation::MOVE &&
      pane_move_session->stage() == PaneMoveStage::TARGET) {
    return pane_move_session->lock_target();
  }
  if (!pane_move_session->confirm(pane_layout)) {
    return false;
  }
  pane_move_session.reset();
  pane_selection.reset();
  pane_maximized = false;
  resize_panes();
  return true;
}

std::optional<PaneMoveSession> const& Tray::move_session() const { return pane_move_session; }

std::optional<PaneSelection> const& Tray::selection() const { return pane_selection; }

PaneLayout const& Tray::layout() const { return pane_layout; }

TrayId const& Tray::id() const { return tray_id; }

TraySnapshot Tray::snapshot() const {
  return TraySnapshot{.id = tray_id,
                      .label = tray_label,
                      .working_directory = cwd,
                      .child_pid = pane(focused_pane).child_pid()};
}

void Tray::set_worktree_management_overlay(std::unique_ptr<WorktreeManagementOverlay> overlay) {
  if (overlay == nullptr) {
    throw std::invalid_argument("tray overlay must not be null");
  }
  worktree_overlay = std::move(overlay);
}

void Tray::clear_worktree_management_overlay() { worktree_overlay.reset(); }

WorktreeManagementOverlay* Tray::worktree_management_overlay() noexcept {
  return worktree_overlay.get();
}

WorktreeManagementOverlay const* Tray::worktree_management_overlay() const noexcept {
  return worktree_overlay.get();
}

Pane const& Tray::pane(PaneId const pane_id) const {
  auto const position = panes.find(pane_id);
  if (position == panes.end()) {
    throw std::out_of_range("pane does not exist in tray");
  }
  return *position->second;
}

Pane& Tray::mutable_pane(PaneId const pane_id) {
  return const_cast<Pane&>(std::as_const(*this).pane(pane_id));
}

PaneId Tray::allocate_pane_id() {
  if (next_pane_value == 0 || next_pane_value == std::numeric_limits<PaneId::Value>::max()) {
    throw std::overflow_error("tray exhausted pane ids");
  }
  PaneId const result = required_pane_id(next_pane_value);
  ++next_pane_value;
  return result;
}

PaneSelection Tray::selection_or_focused_pane() const {
  if (pane_selection.has_value()) {
    return pane_selection.value();
  }
  return PaneSelection::single(pane_layout,
                               required_pane_node(pane_layout.find_pane(focused_pane)));
}

std::string Tray::render_layout(TerminalPosition const origin, base::TerminalSize const size,
                                bool const restore_focused_cursor) const {
  if (pane_maximized) {
    if (restore_focused_cursor && size.rows > 0 && size.cols > 0) {
      return pane(focused_pane).redraw_output_at(origin);
    }
    return pane(focused_pane).preview_output(origin, size);
  }

  std::optional<PaneLayout> move_preview;
  PaneLayout const* rendered_layout = &pane_layout;
  if (pane_move_session.has_value()) {
    move_preview = pane_move_session->preview(pane_layout);
    if (move_preview.has_value()) {
      rendered_layout = &move_preview.value();
    }
  }
  PaneGeometry const geometry =
      calculate_pane_geometry(*rendered_layout, {.origin = origin, .size = size});
  PaneNodeId const focused_node = required_pane_node(rendered_layout->find_pane(focused_pane));
  PaneRegion const& focused_region = geometry.region(focused_node);

  std::string output;
  bool const interaction_active = pane_selection.has_value() || pane_move_session.has_value();
  for (PaneNodeId const node_id : rendered_layout->leaf_nodes()) {
    PaneId const pane_id = rendered_layout->node(node_id).pane_id();
    bool const defer_focused = restore_focused_cursor && !interaction_active &&
                               pane_id == focused_pane && focused_region.size.rows > 0 &&
                               focused_region.size.cols > 0;
    if (!defer_focused) {
      PaneRegion const& region = geometry.region(node_id);
      output += pane(pane_id).preview_output(region.origin, region.size);
    }
  }
  output += render_separators(geometry.separators());
  if (pane_move_session.has_value()) {
    output += render_move_session({.original = pane_layout, .rendered = *rendered_layout}, geometry,
                                  pane_move_session.value(), move_preview.has_value());
  } else if (pane_selection.has_value()) {
    output += render_selection(geometry, pane_selection.value());
  } else if (restore_focused_cursor && focused_region.size.rows > 0 &&
             focused_region.size.cols > 0) {
    output += pane(focused_pane).redraw_output_at(focused_region.origin);
  }
  return output;
}

void Tray::resize_panes() {
  PaneGeometry const geometry = calculate_pane_geometry(
      pane_layout, {.origin = {.row = 0, .column = 0}, .size = current_size});
  for (PaneNodeId const node_id : pane_layout.leaf_nodes()) {
    mutable_pane(pane_layout.node(node_id).pane_id()).resize(pty_size(geometry.region(node_id)));
  }
  if (pane_maximized) {
    mutable_pane(focused_pane)
        .resize({.rows = std::max(current_size.rows, 1), .cols = std::max(current_size.cols, 1)});
  }
}

}  // namespace moe::parent
