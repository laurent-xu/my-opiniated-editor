#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "src/base/terminal_size.h"

namespace moe::parent {

inline constexpr int OVERLAY_FOOTER_HEIGHT = 1;

[[nodiscard]] std::string render_overlay_footer(std::span<std::string_view const> labels,
                                                std::size_t selected_index,
                                                base::TerminalSize parent_size);

}  // namespace moe::parent
