#include "src/parent/pane/pane_selection.h"

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

struct NestedLayout {
  moe::parent::PaneLayout layout;
  moe::parent::PaneNodeId pane_a;
  moe::parent::PaneNodeId pane_b;
  moe::parent::PaneNodeId pane_c;
  moe::parent::PaneNodeId group_bc;
};

NestedLayout make_nested_layout() {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const pane_a = layout.root_id();
  moe::parent::PaneNodeId const pane_b =
      layout.split_leaf(pane_a, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const pane_c =
      layout.split_leaf(pane_b, moe::parent::PaneSplitAxis::TOP_TO_BOTTOM, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const group_bc = required(layout.node(pane_b).parent());
  return {
      .layout = std::move(layout),
      .pane_a = pane_a,
      .pane_b = pane_b,
      .pane_c = pane_c,
      .group_bc = group_bc,
  };
}

TEST(PaneSelectionTest, ExtendingAtRootLevelSelectsWholeNestedSibling) {
  NestedLayout const nested = make_nested_layout();
  moe::parent::PaneSelection const selected_a =
      moe::parent::PaneSelection::single(nested.layout, nested.pane_a);

  moe::parent::PaneSelection const expanded =
      selected_a.step(nested.layout, moe::parent::PaneSiblingDirection::NEXT);

  EXPECT_EQ(expanded.nodes(),
            (std::vector<moe::parent::PaneNodeId>{nested.pane_a, nested.group_bc}));
  EXPECT_TRUE(expanded.contains(nested.group_bc));
  EXPECT_FALSE(expanded.contains(nested.pane_b));
  EXPECT_FALSE(expanded.contains(nested.pane_c));
}

TEST(PaneSelectionTest, CannotSelectAcrossDifferentDirectParents) {
  NestedLayout const nested = make_nested_layout();

  EXPECT_THROW(static_cast<void>(
                   moe::parent::PaneSelection::range(nested.layout, nested.pane_a, nested.pane_b)),
               std::invalid_argument);
}

TEST(PaneSelectionTest, NestedSiblingsCanBeSelectedTogether) {
  NestedLayout const nested = make_nested_layout();

  moe::parent::PaneSelection const selection =
      moe::parent::PaneSelection::range(nested.layout, nested.pane_b, nested.pane_c);

  EXPECT_EQ(selection.parent(), nested.group_bc);
  EXPECT_EQ(selection.nodes(),
            (std::vector<moe::parent::PaneNodeId>{nested.pane_b, nested.pane_c}));
}

TEST(PaneSelectionTest, SteppingActiveEndMaintainsAContiguousRange) {
  moe::parent::PaneLayout layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneNodeId const first = layout.root_id();
  moe::parent::PaneNodeId const second =
      layout.split_leaf(first, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(2),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneNodeId const third =
      layout.split_leaf(second, moe::parent::PaneSplitAxis::LEFT_TO_RIGHT, pane_id(3),
                        moe::parent::PaneInsertion::AFTER);
  moe::parent::PaneSelection const all = moe::parent::PaneSelection::range(layout, first, third);

  moe::parent::PaneSelection const first_two =
      all.step(layout, moe::parent::PaneSiblingDirection::PREVIOUS);
  moe::parent::PaneSelection const first_only =
      first_two.step(layout, moe::parent::PaneSiblingDirection::PREVIOUS);

  EXPECT_EQ(first_two.nodes(), (std::vector<moe::parent::PaneNodeId>{first, second}));
  EXPECT_EQ(first_only.nodes(), (std::vector<moe::parent::PaneNodeId>{first}));
}

TEST(PaneSelectionTest, PromoteAndDescendMoveBetweenTreeLevels) {
  NestedLayout const nested = make_nested_layout();
  moe::parent::PaneSelection const pane_b =
      moe::parent::PaneSelection::single(nested.layout, nested.pane_b);

  moe::parent::PaneSelection const group = pane_b.promote(nested.layout);
  moe::parent::PaneSelection const first_group_child = group.descend(nested.layout);

  EXPECT_EQ(group.nodes(), (std::vector<moe::parent::PaneNodeId>{nested.group_bc}));
  EXPECT_EQ(first_group_child.nodes(), (std::vector<moe::parent::PaneNodeId>{nested.pane_b}));
}

TEST(PaneSelectionTest, RootAndLeafSelectionsCannotMovePastTheirLevel) {
  moe::parent::PaneLayout const layout = moe::parent::PaneLayout::single(pane_id(1));
  moe::parent::PaneSelection const root =
      moe::parent::PaneSelection::single(layout, layout.root_id());

  EXPECT_EQ(root.step(layout, moe::parent::PaneSiblingDirection::NEXT).nodes(), root.nodes());
  EXPECT_EQ(root.promote(layout).nodes(), root.nodes());
  EXPECT_EQ(root.descend(layout).nodes(), root.nodes());
}

}  // namespace
