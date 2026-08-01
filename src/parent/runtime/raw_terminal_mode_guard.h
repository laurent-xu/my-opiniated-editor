#pragma once

#include <termios.h>

#include "src/base/file_descriptor.h"

namespace moe::parent {

class RawTerminalModeGuard {
 public:
  explicit RawTerminalModeGuard(base::FileDescriptor terminal);

  RawTerminalModeGuard(RawTerminalModeGuard const&) = delete;
  RawTerminalModeGuard& operator=(RawTerminalModeGuard const&) = delete;

  ~RawTerminalModeGuard();

 private:
  base::FileDescriptor terminal;
  termios original_mode{};
  bool should_restore = false;
};

}  // namespace moe::parent
