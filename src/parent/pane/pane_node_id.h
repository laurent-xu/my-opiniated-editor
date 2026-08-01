#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace moe::parent {

class PaneNodeId {
 public:
  using Value = std::uint64_t;

  [[nodiscard]] static constexpr std::optional<PaneNodeId> from_value(Value const value) {
    if (value == 0) {
      return std::nullopt;
    }
    return PaneNodeId(value);
  }

  [[nodiscard]] constexpr Value value() const { return raw_value; }

  [[nodiscard]] friend constexpr auto operator<=>(PaneNodeId const& lhs,
                                                  PaneNodeId const& rhs) = default;

 private:
  explicit constexpr PaneNodeId(Value const value) : raw_value(value) {}

  Value raw_value;
};

}  // namespace moe::parent
