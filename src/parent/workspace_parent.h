#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace moe::parent {

std::string startup_banner();
std::string prompt();
std::string handle_command(std::string_view command);
int run_interactive(std::istream& input, std::ostream& output);

}  // namespace moe::parent
