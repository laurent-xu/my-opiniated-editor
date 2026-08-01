#pragma once

#include <cstddef>
#include <string>

namespace moe::parent {

class TerminalTextField {
 public:
  [[nodiscard]] std::string const& value() const noexcept;
  [[nodiscard]] std::size_t cursor_offset() const noexcept;

  void clear() noexcept;
  void set_value(std::string const& value);
  void move_cursor_left() noexcept;
  void move_cursor_right() noexcept;
  void move_cursor_to_start() noexcept;
  void move_cursor_to_end() noexcept;
  void insert(unsigned char byte);
  void backspace();
  void delete_at_cursor();

 private:
  std::string field_value;
  std::size_t field_cursor_offset = 0;
};

}  // namespace moe::parent
