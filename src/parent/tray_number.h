#pragma once

#include <optional>

namespace moe::parent {

class TrayNumber {
 public:
  static constexpr int MIN_VALUE = 1;
  static constexpr int MAX_VALUE = 9;

  [[nodiscard]] static std::optional<TrayNumber> from_int(int value);
  [[nodiscard]] static TrayNumber one();

  [[nodiscard]] int value() const { return raw_value; }
  [[nodiscard]] bool operator==(TrayNumber const& other) const {
    return raw_value == other.raw_value;
  }
  [[nodiscard]] bool operator!=(TrayNumber const& other) const { return !(*this == other); }

 private:
  explicit constexpr TrayNumber(int value) : raw_value(value) {}

  int raw_value;
};

}  // namespace moe::parent
