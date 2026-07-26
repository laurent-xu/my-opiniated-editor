#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "src/parent/content_pty_session.h"

namespace moe::parent {

struct TrayConfig {
  std::vector<std::string> command;
  std::filesystem::path working_directory;
  TerminalSize initial_size;
};

}  // namespace moe::parent
