#pragma once

#include <string>

#include "src/parent/worktree/overlay/terminal_text_field.h"
#include "src/parent/worktree/overlay/workflow/worktree_overlay_mode.h"
#include "src/parent/worktree/overlay/workflow/worktree_overlay_stage.h"

namespace moe::parent {

class WorktreeOverlayWorkflowState {
 public:
  [[nodiscard]] WorktreeOverlayMode mode() const noexcept;
  [[nodiscard]] WorktreeOverlayStage current_stage() const noexcept;
  void set_current_stage(WorktreeOverlayStage stage);

  void reset() noexcept;
  void cycle_mode(int direction) noexcept;

  [[nodiscard]] TerminalTextField* active_text_field() noexcept;
  [[nodiscard]] TerminalTextField const* active_text_field() const noexcept;
  [[nodiscard]] TerminalTextField& branch_field() noexcept;
  [[nodiscard]] TerminalTextField const& branch_field() const noexcept;
  [[nodiscard]] TerminalTextField& repository_root_field() noexcept;
  [[nodiscard]] TerminalTextField const& repository_root_field() const noexcept;
  [[nodiscard]] TerminalTextField& clone_url_field() noexcept;
  [[nodiscard]] TerminalTextField const& clone_url_field() const noexcept;

  [[nodiscard]] std::string& active_error_message() noexcept;
  [[nodiscard]] std::string const& active_error_message() const noexcept;
  [[nodiscard]] std::string& error_message(WorktreeOverlayMode mode) noexcept;
  [[nodiscard]] std::string const& error_message(WorktreeOverlayMode mode) const noexcept;

 private:
  WorktreeOverlayMode active_mode = WorktreeOverlayMode::SWITCH_WORKTREE;
  WorktreeOverlayStage worktree_stage = WorktreeOverlayStage::WORKTREE_REPOSITORY;
  WorktreeOverlayStage repository_stage = WorktreeOverlayStage::REPOSITORY_ROOT;
  TerminalTextField branch;
  TerminalTextField repository_root;
  TerminalTextField clone_url;
  std::string switch_worktree_error;
  std::string worktree_error;
  std::string repository_error;
};

}  // namespace moe::parent
