#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "src/parent/input/command/parent_input_command.h"

namespace moe::parent {

[[nodiscard]] bool is_parent_input_command_prefix(std::uint8_t byte);

[[nodiscard]] std::optional<ParentInputCommand> decode_parent_input_command(
    std::uint8_t command_byte);

[[nodiscard]] std::array<char, 2> encode_parent_input_command(ParentInputCommand const& command);

}  // namespace moe::parent
