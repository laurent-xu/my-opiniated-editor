#pragma once

#include <string_view>
#include <variant>

#include "src/bridge/pty_size.h"
#include "src/parent/input/parent_input_command.h"

namespace moe::bridge::protocol {

struct BrowserTerminalInput {
  std::string_view bytes;
};

struct BrowserTerminalResize {
  PtySize size;
};

using BrowserApplicationMessage =
    std::variant<BrowserTerminalInput, BrowserTerminalResize, parent::ParentInputCommand>;

}  // namespace moe::bridge::protocol
