#pragma once

#include <optional>

namespace moe::parent {

class TrayNumber {
 public:
  [[nodiscard]] static std::optional<TrayNumber> from_int(int value);
  [[nodiscard]] static TrayNumber one();

  [[nodiscard]] int value() const { return raw_value; }

 private:
  explicit constexpr TrayNumber(int value) : raw_value(value) {}

  int raw_value;
};

}  // namespace moe::parent
