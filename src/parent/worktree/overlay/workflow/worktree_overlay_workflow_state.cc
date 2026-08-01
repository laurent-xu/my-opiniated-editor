#include "src/parent/worktree/overlay/workflow/worktree_overlay_workflow_state.h"

#include <stdexcept>
#include <string>

namespace moe::parent {

WorktreeOverlayMode WorktreeOverlayWorkflowState::mode() const noexcept { return active_mode; }

WorktreeOverlayStage WorktreeOverlayWorkflowState::current_stage() const noexcept {
  switch (active_mode) {
    case WorktreeOverlayMode::SWITCH_WORKTREE:
      return WorktreeOverlayStage::SWITCH_WORKTREE;
    case WorktreeOverlayMode::ADD_WORKTREE:
      return worktree_stage;
    case WorktreeOverlayMode::ADD_REPOSITORY:
      return repository_stage;
  }
  return WorktreeOverlayStage::SWITCH_WORKTREE;
}

void WorktreeOverlayWorkflowState::set_current_stage(WorktreeOverlayStage const stage) {
  if (active_mode == WorktreeOverlayMode::ADD_WORKTREE) {
    worktree_stage = stage;
    return;
  }
  if (active_mode == WorktreeOverlayMode::ADD_REPOSITORY) {
    repository_stage = stage;
    return;
  }
  throw std::logic_error("switch-worktree mode does not have a mutable stage");
}

void WorktreeOverlayWorkflowState::reset() noexcept {
  worktree_stage = WorktreeOverlayStage::WORKTREE_REPOSITORY;
  repository_stage = WorktreeOverlayStage::REPOSITORY_ROOT;
  branch.clear();
  repository_root.clear();
  clone_url.clear();
  switch_worktree_error.clear();
  worktree_error.clear();
  repository_error.clear();
}

void WorktreeOverlayWorkflowState::cycle_mode(int const direction) noexcept {
  int constexpr MODE_COUNT = static_cast<int>(WorktreeOverlayMode::ADD_REPOSITORY) + 1;
  int const current = static_cast<int>(active_mode);
  int const next = (current + direction + MODE_COUNT) % MODE_COUNT;
  reset();
  active_mode = static_cast<WorktreeOverlayMode>(next);
}

TerminalTextField* WorktreeOverlayWorkflowState::active_text_field() noexcept {
  switch (current_stage()) {
    case WorktreeOverlayStage::WORKTREE_BRANCH:
      return &branch;
    case WorktreeOverlayStage::REPOSITORY_ROOT:
      return &repository_root;
    case WorktreeOverlayStage::REPOSITORY_CLONE_URL:
      return &clone_url;
    default:
      return nullptr;
  }
}

TerminalTextField const* WorktreeOverlayWorkflowState::active_text_field() const noexcept {
  switch (current_stage()) {
    case WorktreeOverlayStage::WORKTREE_BRANCH:
      return &branch;
    case WorktreeOverlayStage::REPOSITORY_ROOT:
      return &repository_root;
    case WorktreeOverlayStage::REPOSITORY_CLONE_URL:
      return &clone_url;
    default:
      return nullptr;
  }
}

TerminalTextField& WorktreeOverlayWorkflowState::branch_field() noexcept { return branch; }

TerminalTextField const& WorktreeOverlayWorkflowState::branch_field() const noexcept {
  return branch;
}

TerminalTextField& WorktreeOverlayWorkflowState::repository_root_field() noexcept {
  return repository_root;
}

TerminalTextField const& WorktreeOverlayWorkflowState::repository_root_field() const noexcept {
  return repository_root;
}

TerminalTextField& WorktreeOverlayWorkflowState::clone_url_field() noexcept { return clone_url; }

TerminalTextField const& WorktreeOverlayWorkflowState::clone_url_field() const noexcept {
  return clone_url;
}

std::string& WorktreeOverlayWorkflowState::active_error_message() noexcept {
  return error_message(active_mode);
}

std::string const& WorktreeOverlayWorkflowState::active_error_message() const noexcept {
  return error_message(active_mode);
}

std::string& WorktreeOverlayWorkflowState::error_message(WorktreeOverlayMode const mode) noexcept {
  switch (mode) {
    case WorktreeOverlayMode::SWITCH_WORKTREE:
      return switch_worktree_error;
    case WorktreeOverlayMode::ADD_WORKTREE:
      return worktree_error;
    case WorktreeOverlayMode::ADD_REPOSITORY:
      return repository_error;
  }
  return switch_worktree_error;
}

std::string const& WorktreeOverlayWorkflowState::error_message(
    WorktreeOverlayMode const mode) const noexcept {
  switch (mode) {
    case WorktreeOverlayMode::SWITCH_WORKTREE:
      return switch_worktree_error;
    case WorktreeOverlayMode::ADD_WORKTREE:
      return worktree_error;
    case WorktreeOverlayMode::ADD_REPOSITORY:
      return repository_error;
  }
  return switch_worktree_error;
}

}  // namespace moe::parent
