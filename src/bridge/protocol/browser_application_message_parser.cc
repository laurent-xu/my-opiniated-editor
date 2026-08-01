#include "src/bridge/protocol/browser_application_message_parser.h"

#include <charconv>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "src/bridge/protocol/application_message_codec.h"
#include "src/bridge/protocol/browser_to_bridge_message.h"

namespace moe::bridge::protocol {
namespace {

struct JsonKey {
  std::string_view value;
};

std::optional<int> parse_json_int(std::string_view json, JsonKey const key) {
  std::string const quoted_key = "\"" + std::string(key.value) + "\"";
  std::size_t const key_position = json.find(quoted_key);
  if (key_position == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t const colon_position = json.find(':', key_position + quoted_key.size());
  if (colon_position == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t value_start = colon_position + 1U;
  while (value_start < json.size() && json[value_start] == ' ') {
    ++value_start;
  }
  int value = 0;
  char const* const begin = json.data() + value_start;
  char const* const end = json.data() + json.size();
  std::from_chars_result const result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

BrowserTerminalResize parse_resize(std::string_view const payload) {
  std::optional<int> const columns = parse_json_int(payload, JsonKey{.value = "columns"});
  std::optional<int> const rows = parse_json_int(payload, JsonKey{.value = "rows"});
  if (!columns.has_value() || !rows.has_value()) {
    throw std::runtime_error("resize payload requires columns and rows");
  }
  return {.size = {.rows = *rows, .cols = *columns}};
}

parent::ParentInputCommand parse_anonymous_tray_switch(std::string_view const payload) {
  std::optional<int> const tray = parse_json_int(payload, JsonKey{.value = "tray"});
  std::optional<parent::TrayNumber> const tray_number =
      tray.has_value() ? parent::TrayNumber::from_int(*tray) : std::nullopt;
  if (!tray_number.has_value()) {
    throw std::runtime_error("tray switch payload requires tray 1 through 9");
  }
  return parent::SwitchAnonymousTrayCommand{.tray_number = *tray_number};
}

parent::ParentInputCommand parse_worktree_picker_action(std::string_view const action) {
  if (action.size() != 1U) {
    throw std::runtime_error("worktree picker command requires c, r, y, or n");
  }
  switch (action.front()) {
    case 'c':
      return parent::BeginTrayActionCommand{.action = parent::TrayActionIntent::CLEAR};
    case 'r':
      return parent::BeginTrayActionCommand{.action = parent::TrayActionIntent::REMOVE};
    case 'y':
      return parent::ResolveTrayActionCommand{.decision = parent::ConfirmationDecision::CONFIRM};
    case 'n':
      return parent::ResolveTrayActionCommand{.decision = parent::ConfirmationDecision::CANCEL};
    default:
      throw std::runtime_error("worktree picker command requires c, r, y, or n");
  }
}

parent::ParentInputCommand parse_overlay_navigation(std::string_view const navigation) {
  if (navigation == "up") {
    return parent::NavigateOverlayCommand{.navigation = parent::OverlayNavigation::UP};
  }
  if (navigation == "down") {
    return parent::NavigateOverlayCommand{.navigation = parent::OverlayNavigation::DOWN};
  }
  if (navigation == "right") {
    return parent::NavigateOverlayCommand{.navigation = parent::OverlayNavigation::RIGHT};
  }
  if (navigation == "left") {
    return parent::NavigateOverlayCommand{.navigation = parent::OverlayNavigation::LEFT};
  }
  if (navigation == "tab") {
    return parent::NavigateOverlayCommand{.navigation = parent::OverlayNavigation::TAB};
  }
  if (navigation == "backtab") {
    return parent::NavigateOverlayCommand{.navigation = parent::OverlayNavigation::BACKTAB};
  }
  if (navigation == "enter") {
    return parent::NavigateOverlayCommand{.navigation = parent::OverlayNavigation::ENTER};
  }
  throw std::runtime_error("worktree overlay navigation is invalid");
}

parent::ParentInputCommand parse_pane_action(std::string_view const action) {
  using Action = parent::PaneCommandAction;
  if (action == "up") {
    return parent::PaneCommand{.action = Action::UP};
  }
  if (action == "down") {
    return parent::PaneCommand{.action = Action::DOWN};
  }
  if (action == "left") {
    return parent::PaneCommand{.action = Action::LEFT};
  }
  if (action == "right") {
    return parent::PaneCommand{.action = Action::RIGHT};
  }
  if (action == "splitLeftToRight") {
    return parent::PaneCommand{.action = Action::SPLIT_LEFT_TO_RIGHT};
  }
  if (action == "splitAboveBelow") {
    return parent::PaneCommand{.action = Action::SPLIT_ABOVE_BELOW};
  }
  if (action == "toggleSelectionOrSwap") {
    return parent::PaneCommand{.action = Action::TOGGLE_SELECTION_OR_SWAP};
  }
  if (action == "promote") {
    return parent::PaneCommand{.action = Action::PROMOTE};
  }
  if (action == "descend") {
    return parent::PaneCommand{.action = Action::DESCEND};
  }
  if (action == "grow") {
    return parent::PaneCommand{.action = Action::GROW};
  }
  if (action == "shrink") {
    return parent::PaneCommand{.action = Action::SHRINK};
  }
  if (action == "equalize") {
    return parent::PaneCommand{.action = Action::EQUALIZE};
  }
  if (action == "toggleMove") {
    return parent::PaneCommand{.action = Action::TOGGLE_MOVE};
  }
  if (action == "confirmMove") {
    return parent::PaneCommand{.action = Action::CONFIRM_MOVE};
  }
  if (action == "rotate") {
    return parent::PaneCommand{.action = Action::ROTATE};
  }
  if (action == "toggleMaximize") {
    return parent::PaneCommand{.action = Action::TOGGLE_MAXIMIZE};
  }
  if (action == "close") {
    return parent::PaneCommand{.action = Action::CLOSE};
  }
  throw std::runtime_error("pane action is invalid");
}

}  // namespace

std::optional<BrowserApplicationMessage> parse_browser_application_message(
    std::string_view const message) {
  std::optional<BrowserToBridgeMessage> const decoded = decode_browser_to_bridge_message(message);
  if (!decoded.has_value()) {
    return std::nullopt;
  }

  using Type = BrowserToBridgeMessage::Type;
  switch (decoded->type) {
    case Type::TERMINAL_INPUT:
      return BrowserApplicationMessage{BrowserTerminalInput{.bytes = decoded->payload}};
    case Type::RESIZE:
      return BrowserApplicationMessage{parse_resize(decoded->payload)};
    case Type::SWITCH_ANONYMOUS_TRAY:
      return BrowserApplicationMessage{parse_anonymous_tray_switch(decoded->payload)};
    case Type::TOGGLE_WORKTREE_OVERLAY:
      return BrowserApplicationMessage{
          parent::ParentInputCommand{parent::ToggleWorktreeOverlayCommand{}}};
    case Type::TOGGLE_COMMAND_MODE:
      return BrowserApplicationMessage{
          parent::ParentInputCommand{parent::ToggleCommandModeCommand{}}};
    case Type::WORKTREE_PICKER_ACTION:
      return BrowserApplicationMessage{parse_worktree_picker_action(decoded->payload)};
    case Type::OVERLAY_NAVIGATION:
      return BrowserApplicationMessage{parse_overlay_navigation(decoded->payload)};
    case Type::PANE_ACTION:
      return BrowserApplicationMessage{parse_pane_action(decoded->payload)};
  }
  throw std::logic_error("invalid browser-to-bridge message type");
}

}  // namespace moe::bridge::protocol
