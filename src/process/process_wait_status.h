#pragma once

namespace moe::process {

class ProcessWaitStatus {
 public:
  explicit ProcessWaitStatus(int value) noexcept : status_value(value) {}

  [[nodiscard]] int value() const noexcept { return status_value; }

 private:
  int status_value;
};

}  // namespace moe::process
