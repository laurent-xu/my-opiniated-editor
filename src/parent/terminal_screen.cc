#include "src/parent/terminal_screen.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace moe::parent {
namespace {

constexpr std::size_t MAX_SCROLLBACK_LINES = 10000;
constexpr std::string_view CLEAR_TERMINAL_AND_SCROLLBACK = "\x1b[0m\x1b[H\x1b[2J\x1b[3J";
constexpr std::string_view CLEAR_VISIBLE_SCREEN = "\x1b[0m\x1b[H\x1b[2J";
constexpr std::string_view DISABLE_AUTOWRAP = "\x1b[?7l";
constexpr std::string_view ENABLE_AUTOWRAP = "\x1b[?7h";
constexpr std::uint32_t UNICODE_REPLACEMENT_CHARACTER = 0xFFFDU;

void validate_size(TerminalSize const size) {
  if (size.rows <= 0 || size.cols <= 0) {
    throw std::invalid_argument("terminal screen size must have positive rows and columns");
  }
}

int checked_rows(TerminalSize const size) {
  validate_size(size);
  return size.rows;
}

int checked_cols(TerminalSize const size) {
  validate_size(size);
  return size.cols;
}

void append_utf8(std::string& output, std::uint32_t const codepoint) {
  if (codepoint == 0) {
    return;
  }
  std::uint32_t value = codepoint;
  if ((value >= 0xD800U && value <= 0xDFFFU) || value > 0x10FFFFU) {
    value = UNICODE_REPLACEMENT_CHARACTER;
  }
  if (value <= 0x7F) {
    output.push_back(static_cast<char>(value));
    return;
  }
  if (value <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
    output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    return;
  }
  if (value <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
    output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    return;
  }
  output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
  output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
  output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
  output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
}

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

std::size_t complete_utf8_prefix_size(std::string_view const bytes) {
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

std::string cells_to_text(int const cols, VTermScreenCell const* const cells) {
  std::string line;
  for (int col = 0; col < cols; ++col) {
    VTermScreenCell const& cell = cells[col];
    if (cell.width == 0) {
      continue;
    }
    if (cell.chars[0] == 0) {
      line.push_back(' ');
      continue;
    }
    for (std::uint32_t const character : cell.chars) {
      append_utf8(line, character);
    }
  }

  while (!line.empty() && line.back() == ' ') {
    line.pop_back();
  }
  return line;
}

int ignore_damage(VTermRect const /*rect*/, void* const /*user*/) { return 1; }

int ignore_moverect(VTermRect const /*dest*/, VTermRect const /*src*/, void* const /*user*/) {
  return 1;
}

int ignore_movecursor(VTermPos const /*pos*/, VTermPos const /*oldpos*/, int const /*visible*/,
                      void* const /*user*/) {
  return 1;
}

int ignore_termprop(VTermProp const /*prop*/, VTermValue* const /*value*/, void* const /*user*/) {
  return 1;
}

int ignore_bell(void* const /*user*/) { return 1; }

int ignore_resize(int const /*rows*/, int const /*cols*/, void* const /*user*/) { return 1; }

int pop_scrollback_line(int const /*cols*/, VTermScreenCell* const /*cells*/,
                        void* const /*user*/) {
  return 0;
}

int clamp_to_screen(int const value, int const upper_bound) {
  return std::clamp(value, 0, std::max(0, upper_bound - 1));
}

}  // namespace

TerminalScreen::TerminalScreen(TerminalSize const size)
    : screen_size(size), terminal(vterm_new(checked_rows(size), checked_cols(size))) {
  if (terminal == nullptr) {
    throw std::runtime_error("failed to create terminal screen");
  }

  vterm_set_utf8(terminal.get(), 1);
  screen = vterm_obtain_screen(terminal.get());
  state = vterm_obtain_state(terminal.get());
  if (screen == nullptr || state == nullptr) {
    throw std::runtime_error("failed to create terminal screen state");
  }
  configure_screen_callbacks();
  vterm_screen_reset(screen, 1);
}

TerminalScreen::TerminalScreen(TerminalScreen&& other) noexcept
    : screen_size(other.screen_size),
      terminal(std::move(other.terminal)),
      screen(std::exchange(other.screen, nullptr)),
      state(std::exchange(other.state, nullptr)),
      pending_utf8_bytes(std::move(other.pending_utf8_bytes)),
      scrollback_lines(std::move(other.scrollback_lines)) {
  if (screen != nullptr) {
    configure_screen_callbacks();
  }
}

TerminalScreen& TerminalScreen::operator=(TerminalScreen&& other) noexcept {
  if (this != &other) {
    screen_size = other.screen_size;
    terminal = std::move(other.terminal);
    screen = std::exchange(other.screen, nullptr);
    state = std::exchange(other.state, nullptr);
    pending_utf8_bytes = std::move(other.pending_utf8_bytes);
    scrollback_lines = std::move(other.scrollback_lines);
    if (screen != nullptr) {
      configure_screen_callbacks();
    }
  }
  return *this;
}

TerminalScreen::~TerminalScreen() = default;

void TerminalScreen::VTermDeleter::operator()(VTerm* const terminal) const noexcept {
  vterm_free(terminal);
}

void TerminalScreen::configure_screen_callbacks() {
  vterm_screen_set_callbacks(screen, &screen_callbacks(), this);
  vterm_screen_enable_altscreen(screen, 1);
  vterm_screen_enable_reflow(screen, true);
  vterm_screen_set_damage_merge(screen, VTERM_DAMAGE_SCREEN);
}

void TerminalScreen::ingest(std::string_view const bytes) {
  std::string combined_bytes;
  std::string_view input = bytes;
  if (!pending_utf8_bytes.empty()) {
    combined_bytes = pending_utf8_bytes;
    combined_bytes.append(bytes);
    pending_utf8_bytes.clear();
    input = combined_bytes;
  }

  std::size_t const complete_size = complete_utf8_prefix_size(input);
  if (complete_size > 0) {
    std::string const complete_input(input.substr(0, complete_size));
    static_cast<void>(
        vterm_input_write(terminal.get(), complete_input.c_str(), complete_input.size()));
  }
  if (complete_size < input.size()) {
    pending_utf8_bytes.assign(input.substr(complete_size));
  }
  vterm_screen_flush_damage(screen);
}

void TerminalScreen::resize(TerminalSize const size) {
  validate_size(size);
  screen_size = size;
  vterm_set_size(terminal.get(), size.rows, size.cols);
  vterm_screen_flush_damage(screen);
}

std::string TerminalScreen::render_snapshot() const {
  std::string output(CLEAR_TERMINAL_AND_SCROLLBACK);
  output.append(DISABLE_AUTOWRAP);
  for (std::string const& line : scrollback_lines) {
    output.append(line);
    output.append("\r\n");
  }
  if (!scrollback_lines.empty()) {
    for (int row = 0; row < screen_size.rows; ++row) {
      output.append("\r\n");
    }
  }

  output.append(CLEAR_VISIBLE_SCREEN);
  for (int row = 0; row < screen_size.rows; ++row) {
    std::string const line = screen_row_text(row);
    if (line.empty()) {
      continue;
    }
    output.append(cursor_position_sequence(row, 0));
    output.append(line);
  }

  VTermPos cursor{};
  vterm_state_get_cursorpos(state, &cursor);
  output.append(cursor_position_sequence(clamp_to_screen(cursor.row, screen_size.rows),
                                         clamp_to_screen(cursor.col, screen_size.cols)));
  output.append(ENABLE_AUTOWRAP);
  return output;
}

void TerminalScreen::push_scrollback_line(int const cols, void const* const cells) {
  scrollback_lines.push_back(cells_to_text(cols, static_cast<VTermScreenCell const*>(cells)));
  while (scrollback_lines.size() > MAX_SCROLLBACK_LINES) {
    scrollback_lines.pop_front();
  }
}

void TerminalScreen::clear_scrollback() { scrollback_lines.clear(); }

std::string TerminalScreen::screen_row_text(int const row) const {
  std::string line;
  for (int col = 0; col < screen_size.cols; ++col) {
    VTermScreenCell cell{};
    if (vterm_screen_get_cell(screen, VTermPos{.row = row, .col = col}, &cell) == 0) {
      continue;
    }
    if (cell.width == 0) {
      continue;
    }
    if (cell.chars[0] == 0) {
      line.push_back(' ');
      continue;
    }
    for (std::uint32_t const character : cell.chars) {
      append_utf8(line, character);
    }
  }

  while (!line.empty() && line.back() == ' ') {
    line.pop_back();
  }
  return line;
}

std::string TerminalScreen::cursor_position_sequence(int const row, int const col) {
  return "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(col + 1) + "H";
}

VTermScreenCallbacks const& TerminalScreen::screen_callbacks() {
  static VTermScreenCallbacks const CALLBACKS{
      .damage = ignore_damage,
      .moverect = ignore_moverect,
      .movecursor = ignore_movecursor,
      .settermprop = ignore_termprop,
      .bell = ignore_bell,
      .resize = ignore_resize,
      .sb_pushline = push_scrollback_line_callback,
      .sb_popline = pop_scrollback_line,
      .sb_clear = clear_scrollback_callback,
  };
  return CALLBACKS;
}

int TerminalScreen::push_scrollback_line_callback(int const cols,
                                                  VTermScreenCell const* const cells,
                                                  void* const user) {
  static_cast<TerminalScreen*>(user)->push_scrollback_line(cols, cells);
  return 1;
}

int TerminalScreen::clear_scrollback_callback(void* const user) {
  static_cast<TerminalScreen*>(user)->clear_scrollback();
  return 1;
}

}  // namespace moe::parent
