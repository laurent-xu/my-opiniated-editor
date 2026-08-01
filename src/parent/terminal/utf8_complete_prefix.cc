#include "src/parent/terminal/utf8_complete_prefix.h"

#include <algorithm>

namespace moe::parent {

namespace {

bool is_utf8_continuation(unsigned char const byte) { return byte >= 0x80U && byte <= 0xBFU; }

std::size_t utf8_sequence_length(unsigned char const lead) {
  if (lead < 0x80U) {
    return 1;
  }
  if (lead >= 0xC2U && lead <= 0xDFU) {
    return 2;
  }
  if (lead >= 0xE0U && lead <= 0xEFU) {
    return 3;
  }
  if (lead >= 0xF0U && lead <= 0xF4U) {
    return 4;
  }
  return 1;
}

bool valid_utf8_byte_at(std::string_view const bytes, std::size_t const index,
                        std::size_t const offset) {
  auto const lead = static_cast<unsigned char>(bytes[index]);
  auto const value = static_cast<unsigned char>(bytes[index + offset]);
  if (offset != 1) {
    return is_utf8_continuation(value);
  }
  if (!is_utf8_continuation(value)) {
    return false;
  }
  if (lead == 0xE0U) {
    return value >= 0xA0U;
  }
  if (lead == 0xEDU) {
    return value <= 0x9FU;
  }
  if (lead == 0xF0U) {
    return value >= 0x90U;
  }
  if (lead == 0xF4U) {
    return value <= 0x8FU;
  }
  return true;
}

}  // namespace

std::size_t utf8_complete_prefix_size(std::string_view const bytes) {
  std::size_t index = 0;
  while (index < bytes.size()) {
    std::size_t const sequence_length =
        utf8_sequence_length(static_cast<unsigned char>(bytes[index]));
    if (sequence_length == 1) {
      ++index;
      continue;
    }

    std::size_t const available = bytes.size() - index;
    bool invalid_sequence = false;
    for (std::size_t offset = 1; offset < std::min(sequence_length, available); ++offset) {
      if (!valid_utf8_byte_at(bytes, index, offset)) {
        invalid_sequence = true;
        break;
      }
    }
    if (invalid_sequence) {
      ++index;
      continue;
    }
    if (available < sequence_length) {
      return index;
    }

    index += sequence_length;
  }
  return bytes.size();
}

}  // namespace moe::parent
