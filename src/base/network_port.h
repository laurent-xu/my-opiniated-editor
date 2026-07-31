#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace moe::base {

class NetworkPort {
 public:
  using Value = std::uint16_t;

  [[nodiscard]] static constexpr std::optional<NetworkPort> from_int(int const raw_value) {
    if (raw_value < 0 || std::cmp_greater(raw_value, std::numeric_limits<Value>::max())) {
      return std::nullopt;
    }
    return NetworkPort(static_cast<Value>(raw_value));
  }

  [[nodiscard]] constexpr Value value() const { return raw_value; }

  [[nodiscard]] friend constexpr bool operator==(NetworkPort const& lhs,
                                                 NetworkPort const& rhs) = default;

 private:
  constexpr explicit NetworkPort(Value const raw_value) : raw_value(raw_value) {}

  Value raw_value;
};

}  // namespace moe::base
