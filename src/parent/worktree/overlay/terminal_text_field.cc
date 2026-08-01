#include "src/parent/worktree/overlay/terminal_text_field.h"

#include <cstddef>
#include <string>

namespace moe::parent {

namespace {

std::size_t previous_utf8_code_point_start(std::string const& value, std::size_t const offset) {
  if (offset == 0) {
    return 0;
  }
  std::size_t previous = offset - 1U;
  while (previous > 0 && (static_cast<unsigned char>(value[previous]) & 0xC0U) == 0x80U) {
    --previous;
  }
  return previous;
}

std::size_t next_utf8_code_point_end(std::string const& value, std::size_t const offset) {
  if (offset >= value.size()) {
    return value.size();
  }
  std::size_t next = offset + 1U;
  while (next < value.size() && (static_cast<unsigned char>(value[next]) & 0xC0U) == 0x80U) {
    ++next;
  }
  return next;
}

}  // namespace

std::string const& TerminalTextField::value() const noexcept { return field_value; }

std::size_t TerminalTextField::cursor_offset() const noexcept { return field_cursor_offset; }

void TerminalTextField::clear() noexcept {
  field_value.clear();
  field_cursor_offset = 0;
}

void TerminalTextField::set_value(std::string const& value) {
  field_value = value;
  field_cursor_offset = field_value.size();
}

void TerminalTextField::move_cursor_left() noexcept {
  field_cursor_offset = previous_utf8_code_point_start(field_value, field_cursor_offset);
}

void TerminalTextField::move_cursor_right() noexcept {
  field_cursor_offset = next_utf8_code_point_end(field_value, field_cursor_offset);
}

void TerminalTextField::move_cursor_to_start() noexcept { field_cursor_offset = 0; }

void TerminalTextField::move_cursor_to_end() noexcept { field_cursor_offset = field_value.size(); }

void TerminalTextField::insert(unsigned char const byte) {
  field_value.insert(field_cursor_offset, 1U, static_cast<char>(byte));
  ++field_cursor_offset;
}

void TerminalTextField::backspace() {
  std::size_t const previous = previous_utf8_code_point_start(field_value, field_cursor_offset);
  field_value.erase(previous, field_cursor_offset - previous);
  field_cursor_offset = previous;
}

void TerminalTextField::delete_at_cursor() {
  std::size_t const next = next_utf8_code_point_end(field_value, field_cursor_offset);
  field_value.erase(field_cursor_offset, next - field_cursor_offset);
}

}  // namespace moe::parent
