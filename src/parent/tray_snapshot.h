#pragma once

#include <filesystem>
#include <string>

#include "src/base/process_id.h"
#include "src/parent/tray_id.h"

namespace moe::parent {

struct TraySnapshot {
  TrayId id;
  std::string label;
  std::filesystem::path working_directory;
  base::ProcessId child_pid;
};

}  // namespace moe::parent
