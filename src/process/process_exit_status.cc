#include "src/process/process_exit_status.h"

#include <stdexcept>

namespace moe::process {

ProcessExitStatus ProcessExitStatus::exited(int const exit_code) {
  if (exit_code < 0 || exit_code > 255) {
    throw std::invalid_argument("process exit code must be within [0, 255]");
  }
  return ProcessExitStatus(exit_code);
}

ProcessExitStatus ProcessExitStatus::signaled(int const signal_number) {
  if (signal_number <= 0) {
    throw std::invalid_argument("process signal number must be positive");
  }
  return ProcessExitStatus(128 + signal_number);
}

bool ProcessExitStatus::succeeded() const noexcept { return status_value == 0; }

int ProcessExitStatus::value() const noexcept { return status_value; }

ProcessExitStatus::ProcessExitStatus(int const value) : status_value(value) {}

}  // namespace moe::process
