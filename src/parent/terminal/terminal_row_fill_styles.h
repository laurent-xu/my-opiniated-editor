#pragma once

#include <optional>
#include <vector>

#include "src/parent/terminal/terminal_cell_style.h"

namespace moe::parent {

using TerminalRowFillStyles = std::vector<std::optional<CellStyle>>;

}  // namespace moe::parent
