#pragma once

#include <variant>

#include "src/bridge/protocol/browser_terminal_input.h"
#include "src/bridge/protocol/browser_terminal_resize.h"
#include "src/parent/input/command/parent_input_command.h"
#include "src/parent/view/pane_view_protocol.h"

namespace moe::bridge::protocol {

using BrowserApplicationMessage = std::variant<BrowserTerminalInput, BrowserTerminalResize,
                                               parent::PaneViewResize, parent::ParentInputCommand>;

}  // namespace moe::bridge::protocol
