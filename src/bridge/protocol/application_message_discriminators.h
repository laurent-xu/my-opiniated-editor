#pragma once

namespace moe::bridge::protocol::browser_to_bridge_discriminator {

inline constexpr char TERMINAL_INPUT = '0';
inline constexpr char RESIZE = '1';
inline constexpr char SWITCH_ANONYMOUS_TRAY = '2';
inline constexpr char TOGGLE_WORKTREE_OVERLAY = '3';
inline constexpr char TOGGLE_COMMAND_MODE = '5';
inline constexpr char WORKTREE_PICKER_ACTION = '6';
inline constexpr char OVERLAY_NAVIGATION = '7';
inline constexpr char PANE_ACTION = '8';
inline constexpr char PANE_RESIZE = '9';

}  // namespace moe::bridge::protocol::browser_to_bridge_discriminator

namespace moe::bridge::protocol::bridge_to_browser_discriminator {

inline constexpr char TERMINAL_OUTPUT = '0';
inline constexpr char PARENT_STATUS = '1';
inline constexpr char PANE_OUTPUT = '2';

}  // namespace moe::bridge::protocol::bridge_to_browser_discriminator
