#pragma once

#include <string_view>

#include "src/base/terminal_size.h"
#include "src/parent/runtime/parent_command_dispatcher_config.h"

namespace moe::parent::test_support {

inline constexpr base::TerminalSize COMMAND_DISPATCHER_TEST_SIZE{
    .rows = 24,
    .cols = 80,
};

[[nodiscard]] ParentCommandDispatcherConfig command_dispatcher_test_config(
    std::string_view test_name);

}  // namespace moe::parent::test_support
