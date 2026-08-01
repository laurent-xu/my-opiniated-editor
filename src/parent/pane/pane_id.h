#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace moe::parent {

class PaneId {
 public:
  using Value = std::uint64_t;

  [[nodiscard]] static constexpr std::optional<PaneId> from_value(Value const value) {
    if (value == 0) {
      return std::nullopt;
    }
    return PaneId(value);
  }

  [[nodiscard]] constexpr Value value() const { return raw_value; }

  [[nodiscard]] friend constexpr auto operator<=>(PaneId const& lhs, PaneId const& rhs) = default;

 private:
  explicit constexpr PaneId(Value const value) : raw_value(value) {}

  Value raw_value;
};

}  // namespace moe::parent
