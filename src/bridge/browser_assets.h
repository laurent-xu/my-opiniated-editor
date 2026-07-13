#pragma once

#include <string>

namespace moe::bridge {

[[nodiscard]] std::string browser_html();
[[nodiscard]] std::string browser_css();
[[nodiscard]] std::string browser_client_js();

}  // namespace moe::bridge
