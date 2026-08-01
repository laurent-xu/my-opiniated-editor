#include "src/parent/input/parent_input_protocol.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace moe::parent {
namespace {

constexpr std::uint8_t PARENT_INPUT_COMMAND_PREFIX = 0x18U;

char command_byte(ParentInputCommand const& command) {
  return std::visit(
      [](auto const& value) -> char {
        using Command = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Command, ToggleCommandModeCommand>) {
          return 'e';
        } else if constexpr (std::is_same_v<Command, SwitchAnonymousTrayCommand>) {
          return static_cast<char>('0' + value.tray_number.value());
        } else if constexpr (std::is_same_v<Command, ToggleWorktreeOverlayCommand>) {
          return 'w';
        } else if constexpr (std::is_same_v<Command, BeginTrayActionCommand>) {
          return value.action == TrayActionIntent::CLEAR ? 'c' : 'r';
        } else if constexpr (std::is_same_v<Command, ResolveTrayActionCommand>) {
          return value.decision == ConfirmationDecision::CONFIRM ? 'y' : 'n';
        } else if constexpr (std::is_same_v<Command, NavigateOverlayCommand>) {
          switch (value.navigation) {
            case OverlayNavigation::UP:
              return 'A';
            case OverlayNavigation::DOWN:
              return 'B';
            case OverlayNavigation::RIGHT:
              return 'C';
            case OverlayNavigation::LEFT:
              return 'D';
            case OverlayNavigation::TAB:
              return 'I';
            case OverlayNavigation::BACKTAB:
              return 'Z';
            case OverlayNavigation::ENTER:
              return 'M';
          }
          throw std::logic_error("invalid overlay navigation");
        } else if constexpr (std::is_same_v<Command, PaneCommand>) {
          switch (value.action) {
            case PaneCommandAction::UP:
              return 'i';
            case PaneCommandAction::DOWN:
              return 'k';
            case PaneCommandAction::LEFT:
              return 'j';
            case PaneCommandAction::RIGHT:
              return 'l';
            case PaneCommandAction::SPLIT_LEFT_TO_RIGHT:
              return 'v';
            case PaneCommandAction::SPLIT_ABOVE_BELOW:
              return 'h';
            case PaneCommandAction::TOGGLE_SELECTION_OR_SWAP:
              return 's';
            case PaneCommandAction::PROMOTE:
              return '[';
            case PaneCommandAction::DESCEND:
              return ']';
            case PaneCommandAction::GROW:
              return '+';
            case PaneCommandAction::SHRINK:
              return '-';
            case PaneCommandAction::EQUALIZE:
              return '=';
            case PaneCommandAction::TOGGLE_MOVE:
              return 'm';
            case PaneCommandAction::CONFIRM_MOVE:
              return '\r';
            case PaneCommandAction::ROTATE:
              return 't';
            case PaneCommandAction::TOGGLE_MAXIMIZE:
              return 'z';
            case PaneCommandAction::CLOSE:
              return 'x';
          }
          throw std::logic_error("invalid pane command action");
        }
      },
      command);
}

}  // namespace

bool is_parent_input_command_prefix(std::uint8_t const byte) {
  return byte == PARENT_INPUT_COMMAND_PREFIX;
}

std::optional<ParentInputCommand> decode_parent_input_command(std::uint8_t const command_byte) {
  if (command_byte >= '1' && command_byte <= '9') {
    std::optional<TrayNumber> const tray_number =
        TrayNumber::from_int(static_cast<int>(command_byte - '0'));
    if (tray_number.has_value()) {
      return SwitchAnonymousTrayCommand{.tray_number = *tray_number};
    }
  }

  switch (command_byte) {
    case 'e':
      return ToggleCommandModeCommand{};
    case 'w':
      return ToggleWorktreeOverlayCommand{};
    case 'c':
      return BeginTrayActionCommand{.action = TrayActionIntent::CLEAR};
    case 'r':
      return BeginTrayActionCommand{.action = TrayActionIntent::REMOVE};
    case 'y':
      return ResolveTrayActionCommand{.decision = ConfirmationDecision::CONFIRM};
    case 'n':
      return ResolveTrayActionCommand{.decision = ConfirmationDecision::CANCEL};
    case 'A':
      return NavigateOverlayCommand{.navigation = OverlayNavigation::UP};
    case 'B':
      return NavigateOverlayCommand{.navigation = OverlayNavigation::DOWN};
    case 'C':
      return NavigateOverlayCommand{.navigation = OverlayNavigation::RIGHT};
    case 'D':
      return NavigateOverlayCommand{.navigation = OverlayNavigation::LEFT};
    case 'I':
      return NavigateOverlayCommand{.navigation = OverlayNavigation::TAB};
    case 'Z':
      return NavigateOverlayCommand{.navigation = OverlayNavigation::BACKTAB};
    case 'M':
      return NavigateOverlayCommand{.navigation = OverlayNavigation::ENTER};
    case 'i':
      return PaneCommand{.action = PaneCommandAction::UP};
    case 'k':
      return PaneCommand{.action = PaneCommandAction::DOWN};
    case 'j':
      return PaneCommand{.action = PaneCommandAction::LEFT};
    case 'l':
      return PaneCommand{.action = PaneCommandAction::RIGHT};
    case 'v':
      return PaneCommand{.action = PaneCommandAction::SPLIT_LEFT_TO_RIGHT};
    case 'h':
      return PaneCommand{.action = PaneCommandAction::SPLIT_ABOVE_BELOW};
    case 's':
      return PaneCommand{.action = PaneCommandAction::TOGGLE_SELECTION_OR_SWAP};
    case '[':
      return PaneCommand{.action = PaneCommandAction::PROMOTE};
    case ']':
      return PaneCommand{.action = PaneCommandAction::DESCEND};
    case '+':
      return PaneCommand{.action = PaneCommandAction::GROW};
    case '-':
      return PaneCommand{.action = PaneCommandAction::SHRINK};
    case '=':
      return PaneCommand{.action = PaneCommandAction::EQUALIZE};
    case 'm':
      return PaneCommand{.action = PaneCommandAction::TOGGLE_MOVE};
    case '\r':
      return PaneCommand{.action = PaneCommandAction::CONFIRM_MOVE};
    case 't':
      return PaneCommand{.action = PaneCommandAction::ROTATE};
    case 'z':
      return PaneCommand{.action = PaneCommandAction::TOGGLE_MAXIMIZE};
    case 'x':
      return PaneCommand{.action = PaneCommandAction::CLOSE};
    default:
      return std::nullopt;
  }
}

std::array<char, 2> encode_parent_input_command(ParentInputCommand const& command) {
  return {static_cast<char>(PARENT_INPUT_COMMAND_PREFIX), command_byte(command)};
}

}  // namespace moe::parent
