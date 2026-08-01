#include "src/parent/pane/pane_percentage.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <vector>

namespace moe::parent {

namespace {

void validate_weights(std::vector<int> const& weights) {
  if (weights.empty()) {
    throw std::invalid_argument("pane percentages require at least one weight");
  }
  if (std::ranges::any_of(weights, [](int const weight) { return weight < 0; })) {
    throw std::invalid_argument("pane percentage weights cannot be negative");
  }
}

PanePercentage required_percentage(int const value) {
  std::optional<PanePercentage> const percentage = PanePercentage::from_int(value);
  if (!percentage.has_value()) {
    throw std::logic_error("normalized pane percentage is outside 0..100");
  }
  return percentage.value();
}

}  // namespace

std::vector<PanePercentage> normalize_pane_percentages(std::vector<int> const& weights) {
  validate_weights(weights);

  long long const weight_sum = std::accumulate(weights.begin(), weights.end(), 0LL);
  if (weight_sum == 0) {
    return equal_pane_percentages(static_cast<int>(weights.size()));
  }

  std::vector<int> percentages(weights.size(), 0);
  std::vector<long long> remainders(weights.size(), 0);
  int assigned = 0;
  for (std::size_t index = 0; index < weights.size(); ++index) {
    long long const scaled = static_cast<long long>(weights[index]) * PanePercentage::MAX_VALUE;
    percentages[index] = static_cast<int>(scaled / weight_sum);
    remainders[index] = scaled % weight_sum;
    assigned += percentages[index];
  }

  std::vector<std::size_t> remainder_order(weights.size());
  for (std::size_t index = 0; index < remainder_order.size(); ++index) {
    remainder_order[index] = index;
  }
  std::ranges::stable_sort(remainder_order,
                           [&remainders](std::size_t const lhs, std::size_t const rhs) {
                             return remainders[lhs] > remainders[rhs];
                           });

  int const unassigned = PanePercentage::MAX_VALUE - assigned;
  for (int offset = 0; offset < unassigned; ++offset) {
    ++percentages[remainder_order[static_cast<std::size_t>(offset)]];
  }

  std::vector<PanePercentage> result;
  result.reserve(percentages.size());
  for (int const percentage : percentages) {
    result.push_back(required_percentage(percentage));
  }
  return result;
}

std::vector<PanePercentage> equal_pane_percentages(int const count) {
  if (count <= 0) {
    throw std::invalid_argument("equal pane percentages require at least one pane");
  }

  int const base = PanePercentage::MAX_VALUE / count;
  int const remainder = PanePercentage::MAX_VALUE % count;
  std::vector<PanePercentage> result;
  result.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) {
    result.push_back(required_percentage(base + (index < remainder ? 1 : 0)));
  }
  return result;
}

}  // namespace moe::parent
