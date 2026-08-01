#pragma once

#include <optional>
#include <vector>

namespace moe::parent {

class PanePercentage {
 public:
  static constexpr int MIN_VALUE = 0;
  static constexpr int MAX_VALUE = 100;

  [[nodiscard]] static constexpr std::optional<PanePercentage> from_int(int const value) {
    if (value < MIN_VALUE || value > MAX_VALUE) {
      return std::nullopt;
    }
    return PanePercentage(value);
  }

  [[nodiscard]] constexpr int value() const { return raw_value; }

  [[nodiscard]] friend constexpr bool operator==(PanePercentage const& lhs,
                                                 PanePercentage const& rhs) = default;

 private:
  explicit constexpr PanePercentage(int const value) : raw_value(value) {}

  int raw_value;
};

[[nodiscard]] std::vector<PanePercentage> normalize_pane_percentages(
    std::vector<int> const& weights);
[[nodiscard]] std::vector<PanePercentage> equal_pane_percentages(int count);

}  // namespace moe::parent
