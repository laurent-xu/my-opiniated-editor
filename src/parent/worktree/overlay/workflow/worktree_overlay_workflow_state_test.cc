#include "src/parent/worktree/overlay/workflow/worktree_overlay_workflow_state.h"

#include <string>

#include "gtest/gtest.h"

namespace moe::parent {
namespace {

using Direction = WorktreeOverlayModeDirection;

TEST(WorktreeOverlayWorkflowStateTest, StartsInWorktreePickerWithInitialFormStages) {
  WorktreeOverlayWorkflowState state;

  EXPECT_EQ(state.mode(), WorktreeOverlayMode::SWITCH_WORKTREE);
  EXPECT_EQ(state.current_stage(), WorktreeOverlayStage::SWITCH_WORKTREE);
  EXPECT_EQ(state.active_text_field(), nullptr);

  state.cycle_mode(Direction::NEXT);
  EXPECT_EQ(state.mode(), WorktreeOverlayMode::ADD_WORKTREE);
  EXPECT_EQ(state.current_stage(), WorktreeOverlayStage::WORKTREE_REPOSITORY);

  state.cycle_mode(Direction::NEXT);
  EXPECT_EQ(state.mode(), WorktreeOverlayMode::ADD_REPOSITORY);
  EXPECT_EQ(state.current_stage(), WorktreeOverlayStage::REPOSITORY_ROOT);
}

TEST(WorktreeOverlayWorkflowStateTest, CyclingWrapsAndResetsFormState) {
  WorktreeOverlayWorkflowState state;
  state.cycle_mode(Direction::NEXT);
  state.set_current_stage(WorktreeOverlayStage::WORKTREE_BRANCH);
  state.branch_field().set_value("feature");
  state.error_message(WorktreeOverlayMode::SWITCH_WORKTREE) = "switch error";
  state.error_message(WorktreeOverlayMode::ADD_WORKTREE) = "worktree error";
  state.error_message(WorktreeOverlayMode::ADD_REPOSITORY) = "repository error";

  state.cycle_mode(Direction::NEXT);

  EXPECT_EQ(state.mode(), WorktreeOverlayMode::ADD_REPOSITORY);
  EXPECT_EQ(state.current_stage(), WorktreeOverlayStage::REPOSITORY_ROOT);
  EXPECT_TRUE(state.branch_field().value().empty());
  EXPECT_TRUE(state.error_message(WorktreeOverlayMode::SWITCH_WORKTREE).empty());
  EXPECT_TRUE(state.error_message(WorktreeOverlayMode::ADD_WORKTREE).empty());
  EXPECT_TRUE(state.error_message(WorktreeOverlayMode::ADD_REPOSITORY).empty());

  state.cycle_mode(Direction::NEXT);
  EXPECT_EQ(state.mode(), WorktreeOverlayMode::SWITCH_WORKTREE);
  state.cycle_mode(Direction::PREVIOUS);
  EXPECT_EQ(state.mode(), WorktreeOverlayMode::ADD_REPOSITORY);
}

TEST(WorktreeOverlayWorkflowStateTest, CyclesBackwardThroughEveryModeAndWraps) {
  WorktreeOverlayWorkflowState state;

  state.cycle_mode(Direction::PREVIOUS);
  EXPECT_EQ(state.mode(), WorktreeOverlayMode::ADD_REPOSITORY);
  state.cycle_mode(Direction::PREVIOUS);
  EXPECT_EQ(state.mode(), WorktreeOverlayMode::ADD_WORKTREE);
  state.cycle_mode(Direction::PREVIOUS);
  EXPECT_EQ(state.mode(), WorktreeOverlayMode::SWITCH_WORKTREE);
}

TEST(WorktreeOverlayWorkflowStateTest, ResetPreservesModeAndRestoresItsInitialStage) {
  WorktreeOverlayWorkflowState state;
  state.cycle_mode(Direction::NEXT);
  state.set_current_stage(WorktreeOverlayStage::RUNNING);
  state.branch_field().set_value("feature");
  state.active_error_message() = "failed";

  state.reset();

  EXPECT_EQ(state.mode(), WorktreeOverlayMode::ADD_WORKTREE);
  EXPECT_EQ(state.current_stage(), WorktreeOverlayStage::WORKTREE_REPOSITORY);
  EXPECT_TRUE(state.branch_field().value().empty());
  EXPECT_TRUE(state.active_error_message().empty());
}

TEST(WorktreeOverlayWorkflowStateTest, SelectsTextFieldAndErrorForCurrentWorkflow) {
  WorktreeOverlayWorkflowState state;
  state.error_message(WorktreeOverlayMode::SWITCH_WORKTREE) = "switch";
  EXPECT_EQ(&state.active_error_message(),
            &state.error_message(WorktreeOverlayMode::SWITCH_WORKTREE));

  state.cycle_mode(Direction::NEXT);
  state.set_current_stage(WorktreeOverlayStage::WORKTREE_BRANCH);
  EXPECT_EQ(state.active_text_field(), &state.branch_field());
  EXPECT_EQ(&state.active_error_message(), &state.error_message(WorktreeOverlayMode::ADD_WORKTREE));

  state.cycle_mode(Direction::NEXT);
  EXPECT_EQ(state.active_text_field(), &state.repository_root_field());
  state.set_current_stage(WorktreeOverlayStage::REPOSITORY_CLONE_URL);
  EXPECT_EQ(state.active_text_field(), &state.clone_url_field());
  EXPECT_EQ(&state.active_error_message(),
            &state.error_message(WorktreeOverlayMode::ADD_REPOSITORY));
}

}  // namespace
}  // namespace moe::parent
