#pragma once

#include <variant>

#include "src/parent/input/begin_tray_action_command.h"
#include "src/parent/input/navigate_overlay_command.h"
#include "src/parent/input/resolve_tray_action_command.h"
#include "src/parent/input/switch_anonymous_tray_command.h"
#include "src/parent/input/toggle_command_mode_command.h"
#include "src/parent/input/toggle_worktree_overlay_command.h"

namespace moe::parent {

using ParentInputCommand =
    std::variant<ToggleCommandModeCommand, SwitchAnonymousTrayCommand, ToggleWorktreeOverlayCommand,
                 BeginTrayActionCommand, ResolveTrayActionCommand, NavigateOverlayCommand>;

}  // namespace moe::parent
