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
    default:
      return std::nullopt;
  }
}

std::array<char, 2> encode_parent_input_command(ParentInputCommand const& command) {
  return {static_cast<char>(PARENT_INPUT_COMMAND_PREFIX), command_byte(command)};
}

}  // namespace moe::parent
