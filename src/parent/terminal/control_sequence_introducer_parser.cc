#include "src/parent/terminal/control_sequence_introducer_parser.h"

#include <optional>
#include <string_view>

namespace moe::parent {
namespace {

bool is_control_sequence_introducer_final_byte(unsigned char const byte) {
  return byte >= 0x40U && byte <= 0x7EU;
}

bool is_ascii_digit(char const character) { return character >= '0' && character <= '9'; }

std::optional<int> first_control_sequence_introducer_parameter(
    std::string_view const parameter_bytes) {
  int value = 0;
  bool has_digit = false;
  for (char const character : parameter_bytes) {
    if (is_ascii_digit(character)) {
      has_digit = true;
      value = (value * 10) + (character - '0');
      continue;
    }
    if (character == ';' || character == ':') {
      break;
    }
    if (character == '\0') {
      break;
    }
    return std::nullopt;
  }
  return has_digit ? std::optional<int>(value) : std::optional<int>(0);
}

}  // namespace

ControlSequenceIntroducerParseResult parse_control_sequence_introducer_sequence(
    std::string_view const bytes, std::size_t const start) {
  if (start + 1 >= bytes.size()) {
    return ControlSequenceIntroducerParseResult{
        .status = ControlSequenceIntroducerParseStatus::INCOMPLETE};
  }
  if (bytes[start] != '\x1b' || bytes[start + 1] != '[') {
    return ControlSequenceIntroducerParseResult{
        .status = ControlSequenceIntroducerParseStatus::NOT_CONTROL_SEQUENCE_INTRODUCER};
  }

  for (std::size_t index = start + 2; index < bytes.size(); ++index) {
    if (!is_control_sequence_introducer_final_byte(static_cast<unsigned char>(bytes[index]))) {
      continue;
    }

    std::string_view const parameter_bytes = bytes.substr(start + 2, index - (start + 2));
    std::optional<int> const mode = first_control_sequence_introducer_parameter(parameter_bytes);
    if (!mode.has_value()) {
      return ControlSequenceIntroducerParseResult{
          .status = ControlSequenceIntroducerParseStatus::COMPLETE,
          .sequence =
              ControlSequenceIntroducerSequence{
                  .start = start, .end = index + 1, .command = bytes[index]},
      };
    }
    return ControlSequenceIntroducerParseResult{
        .status = ControlSequenceIntroducerParseStatus::COMPLETE,
        .sequence =
            ControlSequenceIntroducerSequence{
                .start = start, .end = index + 1, .command = bytes[index], .mode = *mode},
    };
  }

  return ControlSequenceIntroducerParseResult{.status =
                                                  ControlSequenceIntroducerParseStatus::INCOMPLETE};
}

bool is_erase_sequence(ControlSequenceIntroducerSequence const sequence) {
  return sequence.mode >= 0 && (sequence.command == 'K' || sequence.command == 'J');
}

}  // namespace moe::parent
