#pragma once

#include <string>
#include <vector>

#include "src/process/command_result.h"
#include "src/process/standard_output_mode.h"

namespace moe::process {

[[nodiscard]] CommandResult run_command(std::vector<std::string> const& command,
                                        StandardOutputMode standard_output_mode);

}  // namespace moe::process
