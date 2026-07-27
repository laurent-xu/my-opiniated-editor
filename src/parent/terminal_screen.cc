#include "src/parent/terminal_screen.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace moe::parent {
namespace {

constexpr std::size_t MAX_SCROLLBACK_LINES = 10000;
constexpr std::string_view CLEAR_TERMINAL_AND_SCROLLBACK = "\x1b[0m\x1b[H\x1b[2J\x1b[3J";
constexpr std::string_view CLEAR_VISIBLE_SCREEN = "\x1b[0m\x1b[H\x1b[2J";
constexpr std::string_view ERASE_TO_END_OF_LINE = "\x1b[K";
constexpr std::string_view DISABLE_AUTOWRAP = "\x1b[?7l";
constexpr std::string_view ENABLE_AUTOWRAP = "\x1b[?7h";
constexpr std::string_view ENTER_ALTERNATE_SCREEN = "\x1b[?1049h";
constexpr std::string_view EXIT_ALTERNATE_SCREEN = "\x1b[?1049l";
constexpr std::string_view ENABLE_REVERSE_SCREEN = "\x1b[?5h";
constexpr std::string_view DISABLE_REVERSE_SCREEN = "\x1b[?5l";
constexpr std::string_view SHOW_CURSOR = "\x1b[?25h";
constexpr std::string_view HIDE_CURSOR = "\x1b[?25l";
constexpr std::uint32_t UNICODE_REPLACEMENT_CHARACTER = 0xFFFDU;

enum class TerminalColorKind : std::uint8_t { DEFAULT, INDEXED, RGB };

struct TerminalColor {
  TerminalColorKind kind = TerminalColorKind::DEFAULT;
  int index = 0;
  int red = 0;
  int green = 0;
  int blue = 0;

  [[nodiscard]] bool operator==(TerminalColor const& other) const {
    return kind == other.kind && index == other.index && red == other.red && green == other.green &&
           blue == other.blue;
  }
};

struct CellStyle {
  bool bold = false;
  bool italic = false;
  bool blink = false;
  bool reverse = false;
  bool strike = false;
  int underline = VTERM_UNDERLINE_OFF;
  TerminalColor foreground;
  TerminalColor background;

  [[nodiscard]] bool operator==(CellStyle const& other) const {
    return bold == other.bold && italic == other.italic && blink == other.blink &&
           reverse == other.reverse && strike == other.strike && underline == other.underline &&
           foreground == other.foreground && background == other.background;
  }

  [[nodiscard]] bool is_default() const { return *this == CellStyle{}; }
};

struct CellRange {
  int first_col = 0;
  int last_col = 0;
};

struct SnapshotLineBounds {
  int last_rendered = -1;
  int last_text = -1;
};

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
  if (codepoint == 0 || codepoint == UNICODE_REPLACEMENT_CHARACTER) {
    return;
  }
  if ((codepoint >= 0xD800U && codepoint <= 0xDFFFU) || codepoint > 0x10FFFFU) {
    return;
  }
  std::uint32_t const value = codepoint;
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

TerminalColor terminal_color_from(VTermColor const& color, bool const foreground) {
  if ((foreground && VTERM_COLOR_IS_DEFAULT_FG(&color)) ||
      (!foreground && VTERM_COLOR_IS_DEFAULT_BG(&color))) {
    return TerminalColor{};
  }
  if (VTERM_COLOR_IS_INDEXED(&color)) {
    return TerminalColor{.kind = TerminalColorKind::INDEXED, .index = color.indexed.idx};
  }
  return TerminalColor{.kind = TerminalColorKind::RGB,
                       .red = color.rgb.red,
                       .green = color.rgb.green,
                       .blue = color.rgb.blue};
}

CellStyle cell_style_from(VTermScreenCell const& cell) {
  return CellStyle{
      .bold = cell.attrs.bold != 0,
      .italic = cell.attrs.italic != 0,
      .blink = cell.attrs.blink != 0,
      .reverse = cell.attrs.reverse != 0,
      .strike = cell.attrs.strike != 0,
      .underline = static_cast<int>(cell.attrs.underline),
      .foreground = terminal_color_from(cell.fg, true),
      .background = terminal_color_from(cell.bg, false),
  };
}

void append_color_sgr(std::vector<std::string>& parameters, TerminalColor const& color,
                      bool const foreground) {
  if (color.kind == TerminalColorKind::DEFAULT) {
    parameters.emplace_back(foreground ? "39" : "49");
    return;
  }
  std::string const prefix = foreground ? "38" : "48";
  if (color.kind == TerminalColorKind::INDEXED) {
    parameters.emplace_back(prefix);
    parameters.emplace_back("5");
    parameters.emplace_back(std::to_string(color.index));
    return;
  }
  parameters.emplace_back(prefix);
  parameters.emplace_back("2");
  parameters.emplace_back(std::to_string(color.red));
  parameters.emplace_back(std::to_string(color.green));
  parameters.emplace_back(std::to_string(color.blue));
}

std::string sgr_sequence(CellStyle const& style) {
  if (style.is_default()) {
    return "\x1b[0m";
  }

  std::vector<std::string> parameters;
  parameters.emplace_back("0");
  if (style.bold) {
    parameters.emplace_back("1");
  }
  if (style.italic) {
    parameters.emplace_back("3");
  }
  if (style.underline != VTERM_UNDERLINE_OFF) {
    parameters.emplace_back(style.underline == VTERM_UNDERLINE_DOUBLE ? "21" : "4");
  }
  if (style.blink) {
    parameters.emplace_back("5");
  }
  if (style.reverse) {
    parameters.emplace_back("7");
  }
  if (style.strike) {
    parameters.emplace_back("9");
  }
  append_color_sgr(parameters, style.foreground, true);
  append_color_sgr(parameters, style.background, false);

  std::string sequence = "\x1b[";
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (index > 0) {
      sequence.push_back(';');
    }
    sequence.append(parameters[index]);
  }
  sequence.push_back('m');
  return sequence;
}

bool cell_has_text(VTermScreenCell const& cell) { return cell.chars[0] != 0; }

bool cell_should_be_rendered(VTermScreenCell const& cell) {
  return cell.width != 0 && (cell_has_text(cell) || !cell_style_from(cell).is_default());
}

void append_cell_text(std::string& line, VTermScreenCell const& cell) {
  if (!cell_has_text(cell)) {
    line.push_back(' ');
    return;
  }
  for (std::uint32_t const character : cell.chars) {
    append_utf8(line, character);
  }
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

int last_rendered_cell_index(int const cols, VTermScreenCell const* const cells) {
  int last_rendered = -1;
  for (int col = 0; col < cols; ++col) {
    if (cell_should_be_rendered(cells[col])) {
      last_rendered = col;
    }
  }
  return last_rendered;
}

int last_text_cell_index(int const cols, VTermScreenCell const* const cells) {
  int last_text = -1;
  for (int col = 0; col < cols; ++col) {
    if (cells[col].width != 0 && cell_has_text(cells[col])) {
      last_text = col;
    }
  }
  return last_text;
}

bool blank_cells_have_same_style(CellRange const range, VTermScreenCell const* const cells,
                                 CellStyle const& style) {
  for (int col = range.first_col; col <= range.last_col; ++col) {
    VTermScreenCell const& cell = cells[col];
    if (cell.width == 0 || cell_has_text(cell) || !(cell_style_from(cell) == style)) {
      return false;
    }
  }
  return true;
}

bool should_erase_to_end_of_line(int const cols, VTermScreenCell const* const cells,
                                 SnapshotLineBounds const bounds) {
  if (bounds.last_rendered != cols - 1) {
    return false;
  }

  int const first_trailing_blank = bounds.last_text + 1;
  if (first_trailing_blank > bounds.last_rendered) {
    return false;
  }

  CellStyle const trailing_style = cell_style_from(cells[first_trailing_blank]);
  return !trailing_style.is_default() &&
         blank_cells_have_same_style(
             CellRange{.first_col = first_trailing_blank, .last_col = bounds.last_rendered}, cells,
             trailing_style);
}

std::string cells_to_snapshot_line(int const cols, VTermScreenCell const* const cells) {
  int const last_rendered = last_rendered_cell_index(cols, cells);
  if (last_rendered < 0) {
    return "";
  }

  int const last_text = last_text_cell_index(cols, cells);
  bool const erase_to_end_of_line = should_erase_to_end_of_line(
      cols, cells, SnapshotLineBounds{.last_rendered = last_rendered, .last_text = last_text});
  int const last_cell_to_write = erase_to_end_of_line ? last_text : last_rendered;

  std::string line;
  CellStyle current_style;
  for (int col = 0; col <= last_cell_to_write; ++col) {
    VTermScreenCell const& cell = cells[col];
    if (cell.width == 0) {
      continue;
    }
    CellStyle const next_style = cell_style_from(cell);
    if (!(next_style == current_style)) {
      line.append(sgr_sequence(next_style));
      current_style = next_style;
    }
    append_cell_text(line, cell);
  }
  if (erase_to_end_of_line) {
    CellStyle const erase_style = cell_style_from(cells[last_text + 1]);
    if (!(erase_style == current_style)) {
      line.append(sgr_sequence(erase_style));
      current_style = erase_style;
    }
    line.append(ERASE_TO_END_OF_LINE);
  }
  if (!current_style.is_default()) {
    line.append(sgr_sequence(CellStyle{}));
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
      alternate_screen_active(other.alternate_screen_active),
      reverse_screen_active(other.reverse_screen_active),
      cursor_visible(other.cursor_visible),
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
    alternate_screen_active = other.alternate_screen_active;
    reverse_screen_active = other.reverse_screen_active;
    cursor_visible = other.cursor_visible;
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
  std::string output;
  output.append(alternate_screen_active ? ENTER_ALTERNATE_SCREEN : EXIT_ALTERNATE_SCREEN);
  output.append(reverse_screen_active ? ENABLE_REVERSE_SCREEN : DISABLE_REVERSE_SCREEN);
  output.append(cursor_visible ? SHOW_CURSOR : HIDE_CURSOR);
  output.append(alternate_screen_active ? CLEAR_VISIBLE_SCREEN : CLEAR_TERMINAL_AND_SCROLLBACK);
  output.append(DISABLE_AUTOWRAP);
  if (!alternate_screen_active && !scrollback_lines.empty()) {
    for (std::string const& line : scrollback_lines) {
      output.append(line);
      output.append("\r\n");
    }
    for (int row = 0; row < screen_size.rows; ++row) {
      output.append("\r\n");
    }
    output.append(CLEAR_VISIBLE_SCREEN);
  }

  for (int row = 0; row < screen_size.rows; ++row) {
    std::string const line = screen_row_snapshot_line(row);
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
  scrollback_lines.push_back(
      cells_to_snapshot_line(cols, static_cast<VTermScreenCell const*>(cells)));
  while (scrollback_lines.size() > MAX_SCROLLBACK_LINES) {
    scrollback_lines.pop_front();
  }
}

void TerminalScreen::clear_scrollback() { scrollback_lines.clear(); }

std::string TerminalScreen::screen_row_snapshot_line(int const row) const {
  std::vector<VTermScreenCell> cells(static_cast<std::size_t>(screen_size.cols));
  for (int col = 0; col < screen_size.cols; ++col) {
    static_cast<void>(vterm_screen_get_cell(screen, VTermPos{.row = row, .col = col},
                                            &cells[static_cast<std::size_t>(col)]));
  }
  return cells_to_snapshot_line(screen_size.cols, cells.data());
}

std::string TerminalScreen::cursor_position_sequence(int const row, int const col) {
  return "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(col + 1) + "H";
}

VTermScreenCallbacks const& TerminalScreen::screen_callbacks() {
  static VTermScreenCallbacks const CALLBACKS{
      .damage = ignore_damage,
      .moverect = ignore_moverect,
      .movecursor = ignore_movecursor,
      .settermprop = settermprop_callback,
      .bell = ignore_bell,
      .resize = ignore_resize,
      .sb_pushline = push_scrollback_line_callback,
      .sb_popline = pop_scrollback_line,
      .sb_clear = clear_scrollback_callback,
  };
  return CALLBACKS;
}

int TerminalScreen::settermprop_callback(VTermProp const prop, VTermValue* const value,
                                         void* const user) {
  TerminalScreen& terminal_screen = *static_cast<TerminalScreen*>(user);
  if (prop == VTERM_PROP_ALTSCREEN) {
    terminal_screen.alternate_screen_active = value != nullptr && value->boolean != 0;
    return 1;
  }
  if (prop == VTERM_PROP_REVERSE) {
    terminal_screen.reverse_screen_active = value != nullptr && value->boolean != 0;
    return 1;
  }
  if (prop == VTERM_PROP_CURSORVISIBLE) {
    terminal_screen.cursor_visible = value == nullptr || value->boolean != 0;
    return 1;
  }
  return 1;
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
