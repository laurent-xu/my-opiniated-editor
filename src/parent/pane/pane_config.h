#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "src/base/terminal_size.h"

namespace moe::parent {

struct PaneConfig {
  std::vector<std::string> command;
  std::filesystem::path working_directory;
  base::TerminalSize initial_size;
};

}  // namespace moe::parent
