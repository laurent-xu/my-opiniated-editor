#pragma once

#include <string>

namespace moe::base {

// Removes leading and trailing spaces, tabs, carriage returns, and newlines.
[[nodiscard]] std::string trim_ascii_whitespace(std::string value);

}  // namespace moe::base
