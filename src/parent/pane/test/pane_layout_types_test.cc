#include <numeric>
#include <optional>
#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"
#include "src/parent/pane/pane_id.h"
#include "src/parent/pane/pane_node_id.h"
#include "src/parent/pane/pane_percentage.h"

namespace {

std::vector<int> values(std::vector<moe::parent::PanePercentage> const& percentages) {
  std::vector<int> result;
  result.reserve(percentages.size());
  for (moe::parent::PanePercentage const percentage : percentages) {
    result.push_back(percentage.value());
  }
  return result;
}

template <typename Value>
Value const& required(std::optional<Value> const& value) {
  if (!value.has_value()) {
    throw std::logic_error("expected a value");
  }
  return value.value();
}

TEST(PaneLayoutTypesTest, PaneAndNodeIdsRejectZero) {
  EXPECT_FALSE(moe::parent::PaneId::from_value(0).has_value());
  EXPECT_FALSE(moe::parent::PaneNodeId::from_value(0).has_value());
  std::optional<moe::parent::PaneId> const pane_id = moe::parent::PaneId::from_value(7);
  std::optional<moe::parent::PaneNodeId> const node_id = moe::parent::PaneNodeId::from_value(11);
  EXPECT_EQ(required(pane_id).value(), 7);
  EXPECT_EQ(required(node_id).value(), 11);
}

TEST(PaneLayoutTypesTest, PercentageRejectsValuesOutsideClosedRange) {
  EXPECT_FALSE(moe::parent::PanePercentage::from_int(-1).has_value());
  std::optional<moe::parent::PanePercentage> const zero = moe::parent::PanePercentage::from_int(0);
  std::optional<moe::parent::PanePercentage> const one_hundred =
      moe::parent::PanePercentage::from_int(100);
  EXPECT_EQ(required(zero).value(), 0);
  EXPECT_EQ(required(one_hundred).value(), 100);
  EXPECT_FALSE(moe::parent::PanePercentage::from_int(101).has_value());
}

TEST(PaneLayoutTypesTest, EqualPercentagesRoundLeftToRight) {
  EXPECT_EQ(values(moe::parent::equal_pane_percentages(3)), (std::vector<int>{34, 33, 33}));
  EXPECT_EQ(values(moe::parent::equal_pane_percentages(6)),
            (std::vector<int>{17, 17, 17, 17, 16, 16}));
}

TEST(PaneLayoutTypesTest, NormalizationUsesLargestRemaindersWithStableTies) {
  EXPECT_EQ(values(moe::parent::normalize_pane_percentages({1, 1, 1})),
            (std::vector<int>{34, 33, 33}));
  EXPECT_EQ(values(moe::parent::normalize_pane_percentages({1, 2, 3})),
            (std::vector<int>{17, 33, 50}));
  EXPECT_EQ(values(moe::parent::normalize_pane_percentages({0, 1, 0})),
            (std::vector<int>{0, 100, 0}));
}

TEST(PaneLayoutTypesTest, AllZeroWeightsBecomeEqualPercentages) {
  EXPECT_EQ(values(moe::parent::normalize_pane_percentages({0, 0, 0, 0})),
            (std::vector<int>{25, 25, 25, 25}));
}

TEST(PaneLayoutTypesTest, DistributesArbitraryPercentageTotalWithTheSameRoundingRule) {
  EXPECT_EQ(moe::parent::distribute_pane_percentage_total({1, 1, 1}, 10),
            (std::vector<int>{4, 3, 3}));
  EXPECT_EQ(moe::parent::distribute_pane_percentage_total({0, 0}, 5), (std::vector<int>{3, 2}));
  EXPECT_EQ(moe::parent::distribute_pane_percentage_total({0, 5, 0}, 40),
            (std::vector<int>{0, 40, 0}));
}

TEST(PaneLayoutTypesTest, EveryNormalizationSumsToOneHundred) {
  for (int first = 0; first <= 10; ++first) {
    for (int second = 0; second <= 10; ++second) {
      for (int third = 0; third <= 10; ++third) {
        std::vector<moe::parent::PanePercentage> const normalized =
            moe::parent::normalize_pane_percentages({first, second, third});
        int const sum =
            std::accumulate(normalized.begin(), normalized.end(), 0,
                            [](int const total, moe::parent::PanePercentage const percentage) {
                              return total + percentage.value();
                            });
        EXPECT_EQ(sum, 100);
      }
    }
  }
}

TEST(PaneLayoutTypesTest, NormalizationRejectsMissingOrNegativeWeights) {
  EXPECT_THROW(static_cast<void>(moe::parent::normalize_pane_percentages({})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(moe::parent::normalize_pane_percentages({25, -1, 76})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(moe::parent::equal_pane_percentages(0)), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(moe::parent::distribute_pane_percentage_total({}, 10)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(moe::parent::distribute_pane_percentage_total({1}, 101)),
               std::invalid_argument);
}

}  // namespace
