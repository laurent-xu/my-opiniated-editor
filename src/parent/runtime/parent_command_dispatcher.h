#pragma once

#include "src/base/terminal_size.h"
#include "src/parent/input/command/parent_input_command.h"
#include "src/parent/runtime/parent_command_dispatch_effects.h"
#include "src/parent/runtime/parent_command_dispatcher_config.h"

namespace moe::parent {

class TrayManager;

class ParentCommandDispatcher {
 public:
  ParentCommandDispatcher(TrayManager& manager, ParentCommandDispatcherConfig dispatcher_config);

  ParentCommandDispatcher(ParentCommandDispatcher const&) = delete;
  ParentCommandDispatcher& operator=(ParentCommandDispatcher const&) = delete;

  [[nodiscard]] bool command_mode() const noexcept;
  [[nodiscard]] ParentCommandDispatchEffects dispatch(ParentInputCommand const& command,
                                                      base::TerminalSize size);

 private:
  [[nodiscard]] bool dispatch_pane_command(PaneCommandAction action);
  [[nodiscard]] bool dispatch_pane_direction(PaneCommandAction action);
  [[nodiscard]] bool toggle_worktree_management_overlay(base::TerminalSize size);
  [[nodiscard]] bool resolve_tray_action_confirmation(bool confirmed);

  TrayManager& trays;
  ParentCommandDispatcherConfig config;
  bool command_mode_enabled = false;
};

}  // namespace moe::parent
