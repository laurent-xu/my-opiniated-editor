#pragma once

#include <cstdint>
#include <string_view>

namespace moe::bridge::protocol {

struct BrowserToBridgeMessage {
  enum class Type : std::uint8_t {
    TERMINAL_INPUT,
    RESIZE,
    SWITCH_ANONYMOUS_TRAY,
    TOGGLE_WORKTREE_OVERLAY,
    TOGGLE_COMMAND_MODE,
    WORKTREE_PICKER_ACTION,
    OVERLAY_NAVIGATION,
  };

  Type type;
  std::string_view payload;
};

}  // namespace moe::bridge::protocol
