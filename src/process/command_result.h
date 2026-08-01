#pragma once

#include <string>

#include "src/process/process_exit_status.h"

namespace moe::process {

struct CommandResult {
  ProcessExitStatus exit_status;
  std::string standard_output;
};

}  // namespace moe::process
