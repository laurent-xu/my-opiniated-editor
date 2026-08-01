#include "src/parent/terminal/snapshot/terminal_cell_style.h"

#include <cstddef>
#include <string>
#include <vector>

namespace moe::parent {
namespace {

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

}  // namespace

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

}  // namespace moe::parent
