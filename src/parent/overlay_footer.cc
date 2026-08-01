#include "src/parent/overlay_footer.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace moe::parent {
namespace {

constexpr std::string_view FOOTER_STYLE = "\x1b[48;5;236m\x1b[38;5;252m";
constexpr std::string_view FOOTER_BACKGROUND = "\x1b[48;5;236m";
constexpr std::string_view SELECTED_BACKGROUND = "\x1b[48;5;244m";

}  // namespace

std::string render_overlay_footer(std::span<std::string_view const> const labels,
                                  std::size_t const selected_index,
                                  base::TerminalSize const parent_size) {
  int const row = std::max(parent_size.rows, 1);
  std::size_t const width = static_cast<std::size_t>(std::max(parent_size.cols, 1));
  std::size_t remaining_width = width;

  std::string output = "\x1b[" + std::to_string(row) + ";1H";
  output += FOOTER_STYLE;
  for (std::size_t index = 0; index < labels.size() && remaining_width > 0; ++index) {
    bool const selected = index == selected_index;
    if (selected) {
      output += SELECTED_BACKGROUND;
    }

    std::string_view const displayed = labels[index].substr(0, remaining_width);
    output += displayed;
    remaining_width -= displayed.size();

    if (selected) {
      output += FOOTER_BACKGROUND;
    }
    if (index + 1 < labels.size() && remaining_width > 0) {
      std::size_t const separator_width = std::min<std::size_t>(2, remaining_width);
      output.append(separator_width, ' ');
      remaining_width -= separator_width;
    }
  }

  output.append(remaining_width, ' ');
  output += "\x1b[0m";
  return output;
}

}  // namespace moe::parent
