#pragma once

#include <cstddef>
#include <string_view>

namespace moe::parent {

[[nodiscard]] std::size_t utf8_complete_prefix_size(std::string_view bytes);

}  // namespace moe::parent
