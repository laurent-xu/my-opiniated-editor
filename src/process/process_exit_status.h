#pragma once

#include "src/process/process_wait_status.h"

namespace moe::process {

class ProcessExitStatus {
 public:
  [[nodiscard]] static ProcessExitStatus from_wait_status(ProcessWaitStatus wait_status);
  [[nodiscard]] static ProcessExitStatus exited(int exit_code);
  [[nodiscard]] static ProcessExitStatus signaled(int signal_number);

  [[nodiscard]] bool succeeded() const noexcept;
  [[nodiscard]] int value() const noexcept;

 private:
  explicit ProcessExitStatus(int value);

  int status_value;
};

}  // namespace moe::process
