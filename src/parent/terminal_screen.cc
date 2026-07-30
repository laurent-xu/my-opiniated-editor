#include "src/parent/terminal_screen.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
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
constexpr std::size_t MAX_PENDING_CONTROL_SEQUENCE_INTRODUCER_BYTES = 128;

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

  [[nodiscard]] bool is_default() const { return kind == TerminalColorKind::DEFAULT; }
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

struct ControlSequenceIntroducerSequence {
  std::size_t start = 0;
  std::size_t end = 0;
  char command = '\0';
  int mode = -1;
};

enum class ControlSequenceIntroducerParseStatus : std::uint8_t {
  NOT_CONTROL_SEQUENCE_INTRODUCER,
  INCOMPLETE,
  COMPLETE
};

struct ControlSequenceIntroducerParseResult {
  ControlSequenceIntroducerParseStatus status =
      ControlSequenceIntroducerParseStatus::NOT_CONTROL_SEQUENCE_INTRODUCER;
  ControlSequenceIntroducerSequence sequence;
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

bool penattr_bool(VTermState const* const state, VTermAttr const attr) {
  VTermValue value{};
  return vterm_state_get_penattr(state, attr, &value) != 0 && value.boolean != 0;
}

int penattr_number(VTermState const* const state, VTermAttr const attr, int const fallback) {
  VTermValue value{};
  if (vterm_state_get_penattr(state, attr, &value) == 0) {
    return fallback;
  }
  return value.number;
}

TerminalColor penattr_color(VTermState const* const state, VTermAttr const attr,
                            bool const foreground) {
  VTermValue value{};
  if (vterm_state_get_penattr(state, attr, &value) == 0) {
    return TerminalColor{};
  }
  return terminal_color_from(value.color, foreground);
}

CellStyle current_style_from_state(VTermState const* const state) {
  return CellStyle{
      .bold = penattr_bool(state, VTERM_ATTR_BOLD),
      .italic = penattr_bool(state, VTERM_ATTR_ITALIC),
      .blink = penattr_bool(state, VTERM_ATTR_BLINK),
      .reverse = penattr_bool(state, VTERM_ATTR_REVERSE),
      .strike = penattr_bool(state, VTERM_ATTR_STRIKE),
      .underline = penattr_number(state, VTERM_ATTR_UNDERLINE, VTERM_UNDERLINE_OFF),
      .foreground = penattr_color(state, VTERM_ATTR_FOREGROUND, true),
      .background = penattr_color(state, VTERM_ATTR_BACKGROUND, false),
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

using RowFillStyles = std::vector<std::optional<CellStyle>>;

std::optional<CellStyle> row_fill_style_at(RowFillStyles const* const row_fill_styles,
                                           int const col) {
  if (row_fill_styles == nullptr || col < 0 ||
      static_cast<std::size_t>(col) >= row_fill_styles->size()) {
    return std::nullopt;
  }
  return (*row_fill_styles)[static_cast<std::size_t>(col)];
}

CellStyle effective_cell_style(VTermScreenCell const& cell,
                               std::optional<CellStyle> const fill_style) {
  CellStyle const cell_style = cell_style_from(cell);
  if (!fill_style.has_value()) {
    return cell_style;
  }
  if (cell.width == 0 || (!cell_has_text(cell) && cell_style.is_default())) {
    return *fill_style;
  }
  if (fill_style->background.is_default() || !cell_style.background.is_default()) {
    return cell_style;
  }

  CellStyle merged_style = cell_style;
  merged_style.background = fill_style->background;
  return merged_style;
}

bool cell_should_be_rendered(VTermScreenCell const& cell, CellStyle const& style) {
  return cell.width != 0 && (cell_has_text(cell) || !style.is_default());
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

int last_rendered_cell_index(int const cols, VTermScreenCell const* const cells,
                             RowFillStyles const* const row_fill_styles) {
  int last_rendered = -1;
  for (int col = 0; col < cols; ++col) {
    if (cell_should_be_rendered(
            cells[col],
            effective_cell_style(cells[col], row_fill_style_at(row_fill_styles, col)))) {
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
                                 RowFillStyles const* const row_fill_styles,
                                 CellStyle const& style) {
  for (int col = range.first_col; col <= range.last_col; ++col) {
    VTermScreenCell const& cell = cells[col];
    if (cell.width == 0 || cell_has_text(cell) ||
        !(effective_cell_style(cell, row_fill_style_at(row_fill_styles, col)) == style)) {
      return false;
    }
  }
  return true;
}

bool should_erase_to_end_of_line(int const cols, VTermScreenCell const* const cells,
                                 RowFillStyles const* const row_fill_styles,
                                 SnapshotLineBounds const bounds) {
  if (bounds.last_rendered != cols - 1) {
    return false;
  }

  int const first_trailing_blank = bounds.last_text + 1;
  if (first_trailing_blank > bounds.last_rendered) {
    return false;
  }

  CellStyle const trailing_style = effective_cell_style(
      cells[first_trailing_blank], row_fill_style_at(row_fill_styles, first_trailing_blank));
  return !trailing_style.is_default() &&
         blank_cells_have_same_style(
             CellRange{.first_col = first_trailing_blank, .last_col = bounds.last_rendered}, cells,
             row_fill_styles, trailing_style);
}

std::string cells_to_snapshot_line(int const cols, VTermScreenCell const* const cells,
                                   RowFillStyles const* const row_fill_styles = nullptr,
                                   bool const allow_erase_to_end_of_line = true) {
  int const last_rendered = last_rendered_cell_index(cols, cells, row_fill_styles);
  if (last_rendered < 0) {
    return "";
  }

  int const last_text = last_text_cell_index(cols, cells);
  bool const erase_to_end_of_line =
      allow_erase_to_end_of_line &&
      should_erase_to_end_of_line(
          cols, cells, row_fill_styles,
          SnapshotLineBounds{.last_rendered = last_rendered, .last_text = last_text});
  int const last_cell_to_write = erase_to_end_of_line ? last_text : last_rendered;

  std::string line;
  CellStyle current_style;
  for (int col = 0; col <= last_cell_to_write; ++col) {
    VTermScreenCell const& cell = cells[col];
    if (cell.width == 0) {
      continue;
    }
    CellStyle const next_style =
        effective_cell_style(cell, row_fill_style_at(row_fill_styles, col));
    if (!(next_style == current_style)) {
      line.append(sgr_sequence(next_style));
      current_style = next_style;
    }
    append_cell_text(line, cell);
  }
  if (erase_to_end_of_line) {
    CellStyle const erase_style = effective_cell_style(
        cells[last_text + 1], row_fill_style_at(row_fill_styles, last_text + 1));
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

struct RectMove {
  VTermRect dest;
  VTermRect src;
};

}  // namespace

struct TerminalScreen::LineFillTracker {
  explicit LineFillTracker(TerminalSize const initial_size) { resize(initial_size); }

  void resize(TerminalSize const next_size) {
    size = next_size;
    row_styles.assign(static_cast<std::size_t>(size.rows),
                      RowFillStyles(static_cast<std::size_t>(size.cols)));
  }

  void clear_all() {
    for (RowFillStyles& row : row_styles) {
      std::ranges::fill(row, std::nullopt);
    }
  }

  void record_range(int const row, int const start_col, int const end_col, CellStyle const& style) {
    if (row < 0 || row >= size.rows || end_col < 0 || start_col >= size.cols) {
      return;
    }

    int const clamped_start = clamp_to_screen(start_col, size.cols);
    int const clamped_end = clamp_to_screen(end_col, size.cols);
    RowFillStyles& row_style = row_styles[static_cast<std::size_t>(row)];
    for (int col = clamped_start; col <= clamped_end; ++col) {
      std::optional<CellStyle>& cell_style = row_style[static_cast<std::size_t>(col)];
      if (style.is_default()) {
        cell_style.reset();
      } else {
        cell_style = style;
      }
    }
  }

  void record_rows(int const start_row, int const end_row, CellStyle const& style) {
    if (end_row < 0 || start_row >= size.rows) {
      return;
    }

    int const clamped_start = clamp_to_screen(start_row, size.rows);
    int const clamped_end = clamp_to_screen(end_row, size.rows);
    for (int row = clamped_start; row <= clamped_end; ++row) {
      record_range(row, 0, size.cols - 1, style);
    }
  }

  void move_rect(RectMove const& move) {
    std::vector<RowFillStyles> const old_row_styles = row_styles;
    VTermRect const dest = move.dest;
    VTermRect const src = move.src;
    int const height = std::min(dest.end_row - dest.start_row, src.end_row - src.start_row);
    int const width = std::min(dest.end_col - dest.start_col, src.end_col - src.start_col);
    if (height <= 0 || width <= 0) {
      return;
    }

    for (int row_offset = 0; row_offset < height; ++row_offset) {
      int const dest_row = dest.start_row + row_offset;
      int const src_row = src.start_row + row_offset;
      if (dest_row < 0 || dest_row >= size.rows) {
        continue;
      }

      RowFillStyles& dest_row_styles = row_styles[static_cast<std::size_t>(dest_row)];
      for (int col_offset = 0; col_offset < width; ++col_offset) {
        int const dest_col = dest.start_col + col_offset;
        int const src_col = src.start_col + col_offset;
        if (dest_col < 0 || dest_col >= size.cols) {
          continue;
        }

        std::optional<CellStyle>& dest_style = dest_row_styles[static_cast<std::size_t>(dest_col)];
        if (src_row < 0 || src_row >= size.rows || src_col < 0 || src_col >= size.cols) {
          dest_style.reset();
          continue;
        }
        dest_style =
            old_row_styles[static_cast<std::size_t>(src_row)][static_cast<std::size_t>(src_col)];
      }
    }

    clear_source_cells_outside_dest(move);
  }

  [[nodiscard]] RowFillStyles const* row(int const row) const {
    if (row < 0 || row >= size.rows) {
      return nullptr;
    }
    return &row_styles[static_cast<std::size_t>(row)];
  }

  void clear_source_cells_outside_dest(RectMove const& move) {
    VTermRect const dest = move.dest;
    VTermRect const src = move.src;
    for (int row = src.start_row; row < src.end_row; ++row) {
      if (row < 0 || row >= size.rows) {
        continue;
      }

      RowFillStyles& row_style = row_styles[static_cast<std::size_t>(row)];
      for (int col = src.start_col; col < src.end_col; ++col) {
        if (col < 0 || col >= size.cols ||
            vterm_rect_contains(dest, VTermPos{.row = row, .col = col}) != 0) {
          continue;
        }
        row_style[static_cast<std::size_t>(col)].reset();
      }
    }
  }

  TerminalSize size{};
  std::vector<RowFillStyles> row_styles;
};

TerminalScreen::TerminalScreen(TerminalSize const size)
    : screen_size(size),
      terminal(vterm_new(checked_rows(size), checked_cols(size))),
      line_fill_tracker(std::make_unique<LineFillTracker>(size)) {
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
      scrollback_lines(std::move(other.scrollback_lines)),
      line_fill_tracker(std::move(other.line_fill_tracker)) {
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
    line_fill_tracker = std::move(other.line_fill_tracker);
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
    ingest_complete_input(input.substr(0, complete_size));
  }
  if (complete_size < input.size()) {
    pending_utf8_bytes.assign(input.substr(complete_size));
  }
}

void TerminalScreen::resize(TerminalSize const size) {
  validate_size(size);
  if (screen_size.rows == size.rows && screen_size.cols == size.cols) {
    return;
  }
  screen_size = size;
  vterm_set_size(terminal.get(), size.rows, size.cols);
  line_fill_tracker->resize(size);
  vterm_screen_flush_damage(screen);
}

void TerminalScreen::ingest_complete_input(std::string_view const bytes) {
  std::string combined_bytes;
  std::string_view input = bytes;
  if (!pending_snapshot_control_bytes.empty()) {
    combined_bytes = pending_snapshot_control_bytes;
    combined_bytes.append(bytes);
    pending_snapshot_control_bytes.clear();
    input = combined_bytes;
  }

  std::size_t segment_start = 0;
  std::size_t scan_start = 0;
  while (scan_start < input.size()) {
    std::size_t const escape_start = input.find("\x1b[", scan_start);
    if (escape_start == std::string_view::npos) {
      break;
    }

    ControlSequenceIntroducerParseResult const parse_result =
        parse_control_sequence_introducer_sequence(input, escape_start);
    if (parse_result.status == ControlSequenceIntroducerParseStatus::INCOMPLETE) {
      if (input.size() - escape_start > MAX_PENDING_CONTROL_SEQUENCE_INTRODUCER_BYTES) {
        scan_start = escape_start + 1;
        continue;
      }
      feed_input_to_vterm(input.substr(segment_start, escape_start - segment_start));
      pending_snapshot_control_bytes.assign(input.substr(escape_start));
      return;
    }

    ControlSequenceIntroducerSequence const sequence = parse_result.sequence;
    if (!is_erase_sequence(sequence)) {
      scan_start = sequence.end;
      continue;
    }

    feed_input_to_vterm(input.substr(segment_start, sequence.start - segment_start));
    VTermPos cursor{};
    vterm_state_get_cursorpos(state, &cursor);
    feed_input_to_vterm(input.substr(sequence.start, sequence.end - sequence.start));
    record_erase_for_snapshot(sequence.command, cursor, sequence.mode);
    segment_start = sequence.end;
    scan_start = sequence.end;
  }

  if (segment_start < input.size() && input.back() == '\x1b') {
    feed_input_to_vterm(input.substr(segment_start, input.size() - segment_start - 1));
    pending_snapshot_control_bytes.assign(input.substr(input.size() - 1));
    return;
  }

  feed_input_to_vterm(input.substr(segment_start));
}

void TerminalScreen::feed_input_to_vterm(std::string_view const bytes) {
  if (bytes.empty()) {
    return;
  }
  static_cast<void>(vterm_input_write(terminal.get(), bytes.data(), bytes.size()));
  vterm_screen_flush_damage(screen);
}

void TerminalScreen::record_erase_for_snapshot(char const command, VTermPos const cursor,
                                               int const mode) {
  CellStyle const style = current_style_from_state(state);
  int const row = clamp_to_screen(cursor.row, screen_size.rows);
  int const col = clamp_to_screen(cursor.col, screen_size.cols);

  if (command == 'K') {
    if (mode == 0) {
      line_fill_tracker->record_range(row, col, screen_size.cols - 1, style);
    } else if (mode == 1) {
      line_fill_tracker->record_range(row, 0, col, style);
    } else if (mode == 2) {
      line_fill_tracker->record_range(row, 0, screen_size.cols - 1, style);
    }
    return;
  }

  if (command != 'J') {
    return;
  }
  if (mode == 0) {
    line_fill_tracker->record_range(row, col, screen_size.cols - 1, style);
    line_fill_tracker->record_rows(row + 1, screen_size.rows - 1, style);
  } else if (mode == 1) {
    line_fill_tracker->record_rows(0, row - 1, style);
    line_fill_tracker->record_range(row, 0, col, style);
  } else if (mode == 2 || mode == 3) {
    line_fill_tracker->record_rows(0, screen_size.rows - 1, style);
  }
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
    std::string const line = screen_row_snapshot_line(row, true, screen_size.cols);
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

std::string TerminalScreen::render_region_snapshot(TerminalPosition const origin) const {
  return render_region_snapshot(origin, screen_size, true);
}

std::string TerminalScreen::render_region_snapshot(TerminalPosition const origin,
                                                   TerminalSize const region_size) const {
  return render_region_snapshot(origin, region_size, false);
}

std::string TerminalScreen::render_region_snapshot(TerminalPosition const origin,
                                                   TerminalSize const region_size,
                                                   bool const restore_cursor) const {
  std::string output(HIDE_CURSOR);
  output.append(DISABLE_AUTOWRAP);
  int const region_rows = std::max(region_size.rows, 0);
  int const region_columns = std::max(region_size.cols, 0);
  int const rendered_rows = std::min(region_rows, screen_size.rows);
  int const rendered_columns = std::min(region_columns, screen_size.cols);
  std::string const blank_line(static_cast<std::size_t>(region_columns), ' ');
  for (int row = 0; row < region_rows; ++row) {
    TerminalPosition const output_position{
        .row = origin.row + row,
        .column = origin.column,
    };
    output.append(cursor_position_sequence(output_position.row, output_position.column));
    output.append("\x1b[0m");
    output.append(blank_line);

    if (row >= rendered_rows) {
      continue;
    }
    std::string const line = screen_row_snapshot_line(row, false, rendered_columns);
    if (!line.empty()) {
      output.append(cursor_position_sequence(output_position.row, output_position.column));
      output.append(line);
    }
  }

  if (restore_cursor) {
    VTermPos cursor{};
    vterm_state_get_cursorpos(state, &cursor);
    output.append(
        cursor_position_sequence(origin.row + clamp_to_screen(cursor.row, screen_size.rows),
                                 origin.column + clamp_to_screen(cursor.col, screen_size.cols)));
  } else {
    output.append("\x1b[0m");
  }
  output.append(ENABLE_AUTOWRAP);
  output.append(restore_cursor && cursor_visible ? SHOW_CURSOR : HIDE_CURSOR);
  return output;
}

std::string TerminalScreen::render_blank_region_snapshot(TerminalPosition const origin,
                                                         TerminalSize const region_size) {
  std::string output(HIDE_CURSOR);
  output.append(DISABLE_AUTOWRAP);
  append_blank_region(output, origin, region_size);
  output.append("\x1b[0m");
  output.append(ENABLE_AUTOWRAP);
  output.append(HIDE_CURSOR);
  return output;
}

void TerminalScreen::append_blank_region(std::string& output, TerminalPosition const origin,
                                         TerminalSize const region_size) {
  int const rows = std::max(region_size.rows, 0);
  int const columns = std::max(region_size.cols, 0);
  std::string const blank_line(static_cast<std::size_t>(columns), ' ');
  for (int row = 0; row < rows; ++row) {
    output.append(cursor_position_sequence(origin.row + row, origin.column));
    output.append("\x1b[0;48;5;232m");
    output.append(blank_line);
  }
}

void TerminalScreen::push_scrollback_line(int const cols, void const* const cells) {
  scrollback_lines.push_back(
      cells_to_snapshot_line(cols, static_cast<VTermScreenCell const*>(cells)));
  while (scrollback_lines.size() > MAX_SCROLLBACK_LINES) {
    scrollback_lines.pop_front();
  }
}

void TerminalScreen::clear_scrollback() { scrollback_lines.clear(); }

std::string TerminalScreen::screen_row_snapshot_line(int const row,
                                                     bool const allow_erase_to_end_of_line,
                                                     int const columns) const {
  int const rendered_columns = std::clamp(columns, 0, screen_size.cols);
  std::vector<VTermScreenCell> cells(static_cast<std::size_t>(rendered_columns));
  for (int col = 0; col < rendered_columns; ++col) {
    static_cast<void>(vterm_screen_get_cell(screen, VTermPos{.row = row, .col = col},
                                            &cells[static_cast<std::size_t>(col)]));
  }
  return cells_to_snapshot_line(rendered_columns, cells.data(), line_fill_tracker->row(row),
                                allow_erase_to_end_of_line);
}

std::string TerminalScreen::cursor_position_sequence(int const row, int const col) {
  return "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(col + 1) + "H";
}

VTermScreenCallbacks const& TerminalScreen::screen_callbacks() {
  static VTermScreenCallbacks const CALLBACKS{
      .damage = ignore_damage,
      .moverect = move_rect_callback,
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

int TerminalScreen::move_rect_callback(VTermRect const dest, VTermRect const src,
                                       void* const user) {
  static_cast<TerminalScreen*>(user)->line_fill_tracker->move_rect(
      RectMove{.dest = dest, .src = src});
  return 1;
}

int TerminalScreen::settermprop_callback(VTermProp const prop, VTermValue* const value,
                                         void* const user) {
  TerminalScreen& terminal_screen = *static_cast<TerminalScreen*>(user);
  if (prop == VTERM_PROP_ALTSCREEN) {
    terminal_screen.alternate_screen_active = value != nullptr && value->boolean != 0;
    terminal_screen.line_fill_tracker->clear_all();
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
