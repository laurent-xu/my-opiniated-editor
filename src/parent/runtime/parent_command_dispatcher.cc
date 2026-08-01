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
#include "src/parent/worktree/worktree_remover.h"

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
  return {};
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
