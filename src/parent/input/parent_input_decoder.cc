#include "src/parent/input/parent_input_decoder.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/parent/input/parent_input_command.h"

namespace moe::parent {
namespace {

constexpr unsigned char PARENT_COMMAND_PREFIX = 0x18;

std::optional<ParentInputCommand> command_from_byte(unsigned char const byte) {
  if (byte >= '1' && byte <= '9') {
    std::optional<TrayNumber> const tray_number =
        TrayNumber::from_int(static_cast<int>(byte - '0'));
    if (tray_number.has_value()) {
      return SwitchAnonymousTrayCommand{.tray_number = *tray_number};
    }
  }

  switch (byte) {
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

LiteralInputEvent single_byte_event(unsigned char const byte) {
  return LiteralInputEvent{.bytes = std::string(1, static_cast<char>(byte))};
}

}  // namespace

std::vector<ParentInputEvent> ParentInputDecoder::consume(std::string_view const bytes) {
  std::vector<ParentInputEvent> events;
  std::string literal_bytes;
  literal_bytes.reserve(bytes.size());

  auto flush_literal_bytes = [&]() {
    if (literal_bytes.empty()) {
      return;
    }
    events.emplace_back(LiteralInputEvent{.bytes = std::move(literal_bytes)});
    literal_bytes.clear();
  };

  for (unsigned char const byte : bytes) {
    if (state == State::LITERAL_INPUT) {
      if (byte == PARENT_COMMAND_PREFIX) {
        flush_literal_bytes();
        state = State::COMMAND_BYTE;
      } else {
        literal_bytes.push_back(static_cast<char>(byte));
      }
      continue;
    }

    std::optional<ParentInputCommand> command = command_from_byte(byte);
    if (command.has_value()) {
      events.emplace_back(CommandInputEvent{.command = *command});
    } else {
      events.emplace_back(single_byte_event(PARENT_COMMAND_PREFIX));
      events.emplace_back(single_byte_event(byte));
    }
    state = State::LITERAL_INPUT;
  }

  flush_literal_bytes();
  return events;
}

}  // namespace moe::parent
