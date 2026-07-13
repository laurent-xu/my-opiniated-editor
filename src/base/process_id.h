#pragma once

#include <sys/types.h>

namespace moe::base {

class ProcessId {
 public:
  constexpr explicit ProcessId(pid_t const raw_value = -1) : raw_value(raw_value) {}

  [[nodiscard]] constexpr pid_t value() const { return raw_value; }
  [[nodiscard]] constexpr bool is_child_process() const { return raw_value == 0; }
  [[nodiscard]] constexpr bool is_valid_parent_process() const { return raw_value > 0; }
  [[nodiscard]] constexpr bool is_error() const { return raw_value < 0; }

 private:
  pid_t raw_value;
};

}  // namespace moe::base
