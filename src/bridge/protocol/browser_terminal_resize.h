#pragma once

#include "src/base/terminal_size.h"

namespace moe::bridge::protocol {

struct BrowserTerminalResize {
  base::TerminalSize size;
};

}  // namespace moe::bridge::protocol
