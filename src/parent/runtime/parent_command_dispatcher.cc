#include "src/parent/runtime/parent_command_dispatcher.h"

#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "src/parent/tray/tray_action_kind.h"
#include "src/parent/tray/tray_action_request.h"
#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/tray/tray_manager.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"
#include "src/parent/worktree/removal/worktree_removal_request.h"
#include "src/parent/worktree/removal/worktree_remover.h"

namespace moe::parent {
namespace {

std::string_view overlay_navigation_sequence(OverlayNavigation const navigation) {
  switch (navigation) {
    case OverlayNavigation::UP:
      return "\x1b[A";
    case OverlayNavigation::DOWN:
      return "\x1b[B";
    case OverlayNavigation::RIGHT:
      return "\x1b[C";
    case OverlayNavigation::LEFT:
      return "\x1b[D";
    case OverlayNavigation::TAB:
      return "\t";
    case OverlayNavigation::BACKTAB:
      return "\x1b[Z";
    case OverlayNavigation::ENTER:
      return "\r";
  }
  return {};
}

}  // namespace

ParentCommandDispatcher::ParentCommandDispatcher(TrayManager& manager,
                                                 ParentCommandDispatcherConfig dispatcher_config)
    : trays(manager), config(std::move(dispatcher_config)) {}

bool ParentCommandDispatcher::command_mode() const noexcept { return command_mode_enabled; }

ParentCommandDispatchEffects ParentCommandDispatcher::dispatch(ParentInputCommand const& command,
                                                               base::TerminalSize const size) {
  if (std::holds_alternative<ToggleCommandModeCommand>(command)) {
    WorktreeManagementOverlay* const overlay = trays.active_worktree_management_overlay();
    bool const canceled_action = overlay != nullptr && overlay->has_tray_action_confirmation();
    if (canceled_action) {
      overlay->cancel_tray_action_confirmation();
    }
    command_mode_enabled = !command_mode_enabled;
    return {.publish_status = true, .redraw = canceled_action};
  }
  if (SwitchAnonymousTrayCommand const* const switch_tray =
          std::get_if<SwitchAnonymousTrayCommand>(&command);
      switch_tray != nullptr) {
    static_cast<void>(trays.switch_to(switch_tray->tray_number));
    return {.publish_status = true, .redraw = true};
  }
  if (std::holds_alternative<ToggleWorktreeOverlayCommand>(command)) {
    bool const closed = toggle_worktree_management_overlay(size);
    if (!closed) {
      command_mode_enabled = false;
    }
    return {.publish_status = true, .redraw = true};
  }
  if (NavigateOverlayCommand const* const navigate = std::get_if<NavigateOverlayCommand>(&command);
      navigate != nullptr) {
    WorktreeManagementOverlay* const overlay = trays.active_worktree_management_overlay();
    if (command_mode_enabled && overlay != nullptr) {
      if (overlay->has_tray_action_confirmation()) {
        if (navigate->navigation == OverlayNavigation::ENTER) {
          static_cast<void>(overlay->resolve_tray_action_confirmation(false));
          return {.publish_status = true, .redraw = true};
        }
      } else {
        overlay->write_input(overlay_navigation_sequence(navigate->navigation));
      }
    }
    return {};
  }
  if (BeginTrayActionCommand const* const begin_action =
          std::get_if<BeginTrayActionCommand>(&command);
      begin_action != nullptr) {
    if (!command_mode_enabled) {
      return {};
    }
    WorktreeManagementOverlay* const overlay = trays.active_worktree_management_overlay();
    if (overlay != nullptr) {
      TrayActionKind const kind = begin_action->action == TrayActionIntent::CLEAR
                                      ? TrayActionKind::CLEAR
                                      : TrayActionKind::REMOVE;
      std::optional<TrayId> const highlighted = overlay->highlighted_tray_id();
      if (kind == TrayActionKind::REMOVE && highlighted.has_value() &&
          highlighted->kind() == TrayIdKind::WORKTREE &&
          highlighted->worktree_root() == config.protected_worktree_path) {
        overlay->set_picker_action_error("Remove blocked: this worktree runs my-opiniated-editor");
        return {.publish_status = true, .redraw = true};
      }
      static_cast<void>(overlay->begin_tray_action_confirmation(kind));
    }
    return {.publish_status = true, .redraw = true};
  }
  if (ResolveTrayActionCommand const* const resolve_action =
          std::get_if<ResolveTrayActionCommand>(&command);
      resolve_action != nullptr) {
    if (!command_mode_enabled) {
      return {};
    }
    bool const trays_destroyed =
        resolve_tray_action_confirmation(resolve_action->decision == ConfirmationDecision::CONFIRM);
    return {.publish_status = true, .redraw = true, .trays_destroyed = trays_destroyed};
  }
  if (PaneCommand const* const pane_command = std::get_if<PaneCommand>(&command);
      pane_command != nullptr) {
    if (!command_mode_enabled || trays.active_worktree_management_overlay() != nullptr) {
      return {};
    }
    bool const changed = dispatch_pane_command(pane_command->action);
    return {.publish_status = changed, .redraw = changed};
  }
  return {};
}

bool ParentCommandDispatcher::dispatch_pane_command(PaneCommandAction const action) {
  switch (action) {
    case PaneCommandAction::UP:
    case PaneCommandAction::DOWN:
    case PaneCommandAction::LEFT:
    case PaneCommandAction::RIGHT:
      return dispatch_pane_direction(action);
    case PaneCommandAction::SPLIT_LEFT_TO_RIGHT:
      static_cast<void>(
          trays.split_active_focused_pane(PaneSplitAxis::LEFT_TO_RIGHT, PaneInsertion::AFTER));
      return true;
    case PaneCommandAction::SPLIT_ABOVE_BELOW:
      static_cast<void>(
          trays.split_active_focused_pane(PaneSplitAxis::TOP_TO_BOTTOM, PaneInsertion::AFTER));
      return true;
    case PaneCommandAction::TOGGLE_SELECTION_OR_SWAP:
      if (trays.active_pane_move_session().has_value()) {
        return trays.toggle_active_pane_move_swap();
      }
      return trays.toggle_active_pane_selection();
    case PaneCommandAction::PROMOTE:
      if (trays.active_pane_move_session().has_value()) {
        return trays.promote_active_pane_move_target();
      }
      return trays.promote_active_pane_selection();
    case PaneCommandAction::DESCEND:
      if (trays.active_pane_move_session().has_value()) {
        return trays.descend_active_pane_move_target();
      }
      return trays.descend_active_pane_selection();
    case PaneCommandAction::GROW:
      return trays.resize_active_pane_selection(5);
    case PaneCommandAction::SHRINK:
      return trays.resize_active_pane_selection(-5);
    case PaneCommandAction::EQUALIZE:
      return trays.equalize_active_pane_selection();
    case PaneCommandAction::TOGGLE_MOVE:
      return trays.toggle_active_pane_move();
    case PaneCommandAction::CONFIRM_MOVE:
      return trays.advance_active_pane_move();
    case PaneCommandAction::ROTATE:
      return trays.rotate_active_pane_level();
    case PaneCommandAction::TOGGLE_MAXIMIZE:
      return trays.toggle_active_focused_pane_maximized();
    case PaneCommandAction::CLOSE:
      return trays.close_active_focused_pane();
  }
  return false;
}

bool ParentCommandDispatcher::dispatch_pane_direction(PaneCommandAction const action) {
  PaneFocusDirection direction;
  switch (action) {
    case PaneCommandAction::UP:
      direction = PaneFocusDirection::UP;
      break;
    case PaneCommandAction::DOWN:
      direction = PaneFocusDirection::DOWN;
      break;
    case PaneCommandAction::LEFT:
      direction = PaneFocusDirection::LEFT;
      break;
    case PaneCommandAction::RIGHT:
      direction = PaneFocusDirection::RIGHT;
      break;
    default:
      return false;
  }
  std::optional<PaneMoveSession> const& move = trays.active_pane_move_session();
  if (move.has_value()) {
    if (move->stage() == PaneMoveStage::TARGET) {
      return trays.step_active_pane_move_target(direction);
    }
    switch (direction) {
      case PaneFocusDirection::UP:
        return trays.set_active_pane_move_drop_direction(PaneDropDirection::UP);
      case PaneFocusDirection::DOWN:
        return trays.set_active_pane_move_drop_direction(PaneDropDirection::DOWN);
      case PaneFocusDirection::LEFT:
        return trays.set_active_pane_move_drop_direction(PaneDropDirection::LEFT);
      case PaneFocusDirection::RIGHT:
        return trays.set_active_pane_move_drop_direction(PaneDropDirection::RIGHT);
    }
  }
  if (trays.active_pane_selection().has_value()) {
    return trays.step_active_pane_selection(direction);
  }
  return trays.focus_active_pane_direction(direction);
}

bool ParentCommandDispatcher::toggle_worktree_management_overlay(base::TerminalSize const size) {
  if (trays.active_worktree_management_overlay() != nullptr) {
    trays.clear_active_worktree_management_overlay();
    return true;
  }

  std::filesystem::path const working_directory = trays.active_snapshot().working_directory;
  trays.set_active_worktree_management_overlay(WorktreeManagementOverlay::start(
      config.parent_executable, config.worktree_registry_path, working_directory,
      config.git_executable, config.fzf_executable, trays.tray_snapshots(), size));
  return false;
}

bool ParentCommandDispatcher::resolve_tray_action_confirmation(bool const confirmed) {
  WorktreeManagementOverlay* overlay = trays.active_worktree_management_overlay();
  if (overlay == nullptr || !overlay->has_tray_action_confirmation()) {
    return false;
  }
  std::optional<TrayActionRequest> const request =
      overlay->resolve_tray_action_confirmation(confirmed);
  if (!request.has_value()) {
    return false;
  }

  bool const target_is_active = request->tray_id == trays.active_id();
  if (request->kind == TrayActionKind::REMOVE && request->tray_id.kind() == TrayIdKind::WORKTREE) {
    try {
      WorktreeRemover(config.git_executable)
          .remove(WorktreeRemovalRequest{
              .registry_path = config.worktree_registry_path,
              .worktree_path = request->tray_id.worktree_root(),
              .protected_worktree_path = config.protected_worktree_path,
          });
    } catch (std::exception const& error) {
      overlay->set_picker_action_error("Remove failed: " + std::string(error.what()));
      return false;
    }
  }

  bool const destroyed = trays.destroy_tray(request->tray_id);
  if (!destroyed && request->kind == TrayActionKind::CLEAR) {
    overlay->set_picker_action_error("Tray has no in-session content");
    return false;
  }
  if (!target_is_active) {
    overlay = trays.active_worktree_management_overlay();
    if (overlay != nullptr) {
      overlay->refresh_worktree_picker();
    }
  }
  return destroyed;
}

}  // namespace moe::parent
