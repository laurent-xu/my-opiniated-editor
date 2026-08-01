#include "src/parent/runtime/raw_terminal_mode_guard.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace moe::parent {

RawTerminalModeGuard::RawTerminalModeGuard(base::FileDescriptor const terminal)
    : terminal(terminal) {
  if (!terminal.is_valid() || isatty(terminal.value()) == 0) {
    return;
  }
  if (tcgetattr(terminal.value(), &original_mode) != 0) {
    throw std::runtime_error(std::string("failed to read terminal mode: ") + std::strerror(errno));
  }

  termios raw_mode = original_mode;
  cfmakeraw(&raw_mode);
  if (tcsetattr(terminal.value(), TCSANOW, &raw_mode) != 0) {
    throw std::runtime_error(std::string("failed to set raw terminal mode: ") +
                             std::strerror(errno));
  }
  should_restore = true;
}

RawTerminalModeGuard::~RawTerminalModeGuard() {
  if (should_restore) {
    static_cast<void>(tcsetattr(terminal.value(), TCSANOW, &original_mode));
  }
}

}  // namespace moe::parent
