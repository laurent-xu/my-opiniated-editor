#include "src/parent/pane/pane_rotate.h"

#include <optional>
#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"

namespace {

template <typename Value>
Value const& required(std::optional<Value> const& value) {
  if (!value.has_value()) {
    throw std::logic_error("expected a value");
  }
  return value.value();
}

moe::parent::PaneId pane_id(moe::parent::PaneId::Value const value) {
  return required(moe::parent::PaneId::from_value(value));
}

std::vector<int> percentages(moe::parent::PaneSplit const& split) {
  std::vector<int> result;
  result.reserve(split.children.size());
  for (moe::parent::PaneSplitChild const& child : split.children) {
    result.push_back(child.percentage.value());
  }
  return result;
}

std::vector<moe::parent::PaneNodeId> child_node_ids(moe::parent::PaneSplit const& split) {
  std::vector<moe::parent::PaneNodeId> result;
  result.reserve(split.children.size());
  for (moe::parent::PaneSplitChild const& child : split.children) {
    result.push_back(child.node_id);
  }
  return result;
}

TEST(PaneRotateTest, RotatesSelectedHierarchyIntoParentAndBack) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const vertical = required(layout.node(pane_b).parent());
  layout.set_split_percentages(vertical, {60, 40});
  moe::parent::PaneSelection selection = moe::parent::PaneSelection::single(layout, vertical);

  ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  EXPECT_EQ(root.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(child_node_ids(root), (std::vector<moe::parent::PaneNodeId>{pane_a, pane_b, pane_c}));
  EXPECT_EQ(percentages(root), (std::vector<int>{50, 30, 20}));
  EXPECT_EQ(selection.nodes(), (std::vector<moe::parent::PaneNodeId>{pane_b, pane_c}));
  EXPECT_EQ(selection.parent(), layout.root_id());
  EXPECT_THROW(static_cast<void>(layout.node(vertical)), std::out_of_range);

  ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

  moe::parent::PaneSplit const& restored_root = layout.node(layout.root_id()).split();
  ASSERT_EQ(restored_root.children.size(), 2U);
  EXPECT_EQ(restored_root.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(restored_root.children[0].node_id, pane_a);
  EXPECT_EQ(percentages(restored_root), (std::vector<int>{50, 50}));
  ASSERT_EQ(selection.nodes().size(), 1U);
  moe::parent::PaneNodeId const restored_vertical = selection.nodes().front();
  EXPECT_EQ(restored_root.children[1].node_id, restored_vertical);
  moe::parent::PaneSplit const& restored = layout.node(restored_vertical).split();
  EXPECT_EQ(restored.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(restored), (std::vector<moe::parent::PaneNodeId>{pane_b, pane_c}));
  EXPECT_EQ(percentages(restored), (std::vector<int>{60, 40}));
}

TEST(PaneRotateTest, RotatesEveryAxisInSelectedRootAndRestoresItExactly) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const root_id = layout.root_id();
  moe::parent::PaneNodeId const vertical = required(layout.node(pane_b).parent());
  layout.set_split_percentages(root_id, {45, 55});
  layout.set_split_percentages(vertical, {60, 40});
  moe::parent::PaneSelection selection = moe::parent::PaneSelection::single(layout, root_id);

  ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

  moe::parent::PaneSplit const& rotated_root = layout.node(root_id).split();
  EXPECT_EQ(rotated_root.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(rotated_root), (std::vector<moe::parent::PaneNodeId>{pane_a, vertical}));
  EXPECT_EQ(percentages(rotated_root), (std::vector<int>{45, 55}));
  moe::parent::PaneSplit const& rotated_nested = layout.node(vertical).split();
  EXPECT_EQ(rotated_nested.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(child_node_ids(rotated_nested), (std::vector<moe::parent::PaneNodeId>{pane_b, pane_c}));
  EXPECT_EQ(percentages(rotated_nested), (std::vector<int>{60, 40}));
  EXPECT_EQ(selection.nodes(), (std::vector<moe::parent::PaneNodeId>{root_id}));

  ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

  moe::parent::PaneSplit const& restored_root = layout.node(root_id).split();
  EXPECT_EQ(restored_root.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(child_node_ids(restored_root),
            (std::vector<moe::parent::PaneNodeId>{pane_a, vertical}));
  EXPECT_EQ(percentages(restored_root), (std::vector<int>{45, 55}));
  moe::parent::PaneSplit const& restored_nested = layout.node(vertical).split();
  EXPECT_EQ(restored_nested.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(restored_nested),
            (std::vector<moe::parent::PaneNodeId>{pane_b, pane_c}));
  EXPECT_EQ(percentages(restored_nested), (std::vector<int>{60, 40}));
}

TEST(PaneRotateTest, RotatesOnlySelectedSiblingPercentagesAndRestoresThem) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  layout.set_split_percentages(layout.root_id(), {20, 30, 50});
  moe::parent::PaneSelection selection = moe::parent::PaneSelection::range(layout, pane_a, pane_b);

  ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  ASSERT_EQ(root.children.size(), 2U);
  EXPECT_EQ(root.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(root.children[1].node_id, pane_c);
  EXPECT_EQ(percentages(root), (std::vector<int>{50, 50}));
  ASSERT_EQ(selection.nodes().size(), 1U);
  moe::parent::PaneNodeId const rotated_group = selection.nodes().front();
  EXPECT_EQ(root.children[0].node_id, rotated_group);
  moe::parent::PaneSplit const& selected = layout.node(rotated_group).split();
  EXPECT_EQ(selected.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(selected), (std::vector<moe::parent::PaneNodeId>{pane_a, pane_b}));
  EXPECT_EQ(percentages(selected), (std::vector<int>{40, 60}));

  ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

  moe::parent::PaneSplit const& restored = layout.node(layout.root_id()).split();
  EXPECT_EQ(restored.axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);
  EXPECT_EQ(child_node_ids(restored),
            (std::vector<moe::parent::PaneNodeId>{pane_a, pane_b, pane_c}));
  EXPECT_EQ(percentages(restored), (std::vector<int>{20, 30, 50}));
  EXPECT_EQ(selection.nodes(), (std::vector<moe::parent::PaneNodeId>{pane_a, pane_b}));
}

TEST(PaneRotateTest, RotatesNestedSplitsInsideAPartialSelectionAndRestoresThem) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const nested = required(layout.node(pane_b).parent());
  moe::parent::PaneNodeId const pane_d =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(4),
                        moe::parent::PaneInsertion::AFTER);
  layout.set_split_percentages(layout.root_id(), {20, 30, 50});
  layout.set_split_percentages(nested, {60, 40});
  moe::parent::PaneSelection selection = moe::parent::PaneSelection::range(layout, pane_d, nested);

  ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

  moe::parent::PaneSplit const& root = layout.node(layout.root_id()).split();
  ASSERT_EQ(root.children.size(), 2U);
  EXPECT_EQ(percentages(root), (std::vector<int>{20, 80}));
  ASSERT_EQ(selection.nodes().size(), 1U);
  moe::parent::PaneNodeId const rotated_group = selection.nodes().front();
  EXPECT_EQ(root.children[1].node_id, rotated_group);
  moe::parent::PaneSplit const& group = layout.node(rotated_group).split();
  EXPECT_EQ(group.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(group), (std::vector<moe::parent::PaneNodeId>{pane_d, nested}));
  EXPECT_EQ(percentages(group), (std::vector<int>{38, 62}));
  EXPECT_EQ(layout.node(nested).split().axis, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT);

  ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

  moe::parent::PaneSplit const& restored_root = layout.node(layout.root_id()).split();
  EXPECT_EQ(child_node_ids(restored_root),
            (std::vector<moe::parent::PaneNodeId>{pane_a, pane_d, nested}));
  EXPECT_EQ(percentages(restored_root), (std::vector<int>{20, 30, 50}));
  moe::parent::PaneSplit const& restored_nested = layout.node(nested).split();
  EXPECT_EQ(restored_nested.axis, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM);
  EXPECT_EQ(child_node_ids(restored_nested),
            (std::vector<moe::parent::PaneNodeId>{pane_b, pane_c}));
  EXPECT_EQ(percentages(restored_nested), (std::vector<int>{60, 40}));
}

TEST(PaneRotateTest, EveryIntegerPercentageDistributionRestoresTheExcludedSibling) {
  for (int excluded = 0; excluded <= 100; ++excluded) {
    for (int selected_first = 0; selected_first <= 100 - excluded; ++selected_first) {
      int const selected_second = 100 - excluded - selected_first;
      SCOPED_TRACE(::testing::Message()
                   << "excluded=" << excluded << " selected_first=" << selected_first
                   << " selected_second=" << selected_second);
      moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
      moe::parent::PaneNodeId const pane_a = layout.root_id();
      moe::parent::PaneNodeId const pane_b =
          layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                            moe::parent::PaneInsertion::AFTER);
      moe::parent::PaneNodeId const pane_c =
          layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                            moe::parent::PaneInsertion::AFTER);
      layout.set_split_percentages(layout.root_id(), {excluded, selected_first, selected_second});
      moe::parent::PaneSelection selection =
          moe::parent::PaneSelection::range(layout, pane_b, pane_c);

      ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));
      ASSERT_TRUE(moe::parent::rotate_pane_selection_level(layout, selection));

      EXPECT_EQ(percentages(layout.node(layout.root_id()).split()),
                (std::vector<int>{excluded, selected_first, selected_second}));
    }
  }
}

TEST(PaneRotateTest, EveryThreePanePercentageRangeRoundTripsExactly) {
  for (int selected_total = 0; selected_total <= 100; ++selected_total) {
    for (int first = 0; first <= selected_total; ++first) {
      for (int second = 0; second <= selected_total - first; ++second) {
        int const third = selected_total - first - second;
        std::vector<moe::parent::PanePercentage> const normalized =
            moe::parent::normalize_pane_percentages({first, second, third});
        std::vector<int> normalized_values;
        normalized_values.reserve(normalized.size());
        for (moe::parent::PanePercentage const percentage : normalized) {
          normalized_values.push_back(percentage.value());
        }
        std::vector<int> const restored =
            moe::parent::distribute_pane_percentage_total(normalized_values, selected_total);
        if (restored != std::vector<int>{first, second, third}) {
          ADD_FAILURE() << "selected_total=" << selected_total << " weights=" << first << ','
                        << second << ',' << third;
          return;
        }
      }
    }
  }
}

TEST(PaneRotateTest, DoesNothingForOnlyPane) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneSelection selection =
      moe::parent::PaneSelection::single(layout, layout.root_id());
  EXPECT_FALSE(moe::parent::rotate_pane_selection_level(layout, selection));
}

}  // namespace
