#pragma once

#include <variant>

#include "src/parent/input/command/begin_tray_action_command.h"
#include "src/parent/input/command/navigate_overlay_command.h"
#include "src/parent/input/command/pane_command.h"
#include "src/parent/input/command/resolve_tray_action_command.h"
#include "src/parent/input/command/switch_anonymous_tray_command.h"
#include "src/parent/input/command/toggle_command_mode_command.h"
#include "src/parent/input/command/toggle_worktree_overlay_command.h"

namespace moe::parent {

using ParentInputCommand =
    std::variant<ToggleCommandModeCommand, SwitchAnonymousTrayCommand, ToggleWorktreeOverlayCommand,
                 BeginTrayActionCommand, ResolveTrayActionCommand, NavigateOverlayCommand,
                 PaneCommand>;

}  // namespace moe::parent
