#include "src/parent/input/parent_input_decoder.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/parent/input/parent_input_protocol.h"

namespace moe::parent {
namespace {

LiteralInputEvent single_byte_event(std::uint8_t const byte) {
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

  for (std::uint8_t const byte : bytes) {
    if (state == State::LITERAL_INPUT) {
      if (is_parent_input_command_prefix(byte)) {
        flush_literal_bytes();
        pending_command_prefix = byte;
        state = State::COMMAND_BYTE;
      } else {
        literal_bytes.push_back(static_cast<char>(byte));
      }
      continue;
    }

    std::optional<ParentInputCommand> command = decode_parent_input_command(byte);
    if (command.has_value()) {
      events.emplace_back(CommandInputEvent{.command = *command});
    } else {
      events.emplace_back(single_byte_event(pending_command_prefix));
      events.emplace_back(single_byte_event(byte));
    }
    state = State::LITERAL_INPUT;
  }

  flush_literal_bytes();
  return events;
}

}  // namespace moe::parent
