#pragma once

namespace moe::base {

class FileDescriptor {
 public:
  constexpr explicit FileDescriptor(int const raw_value = -1) : raw_value(raw_value) {}

  [[nodiscard]] constexpr int value() const { return raw_value; }
  [[nodiscard]] constexpr bool is_valid() const { return raw_value >= 0; }

 private:
  int raw_value;
};

}  // namespace moe::base
