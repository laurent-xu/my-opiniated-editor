#include "src/parent/tray/tray.h"

#include <stdexcept>
#include <utility>

#include "src/parent/pane/pane.h"
#include "src/parent/worktree/overlay/worktree_management_overlay.h"

namespace moe::parent {

std::unique_ptr<Tray> Tray::start(TrayId id, TrayConfig const& config) {
  std::unique_ptr<Pane> pane = Pane::start(config);
  return std::unique_ptr<Tray>(new Tray(std::move(id), config.working_directory, std::move(pane)));
}

Tray::Tray(TrayId id, std::filesystem::path working_directory, std::unique_ptr<Pane> initial_pane)
    : tray_id(std::move(id)),
      tray_label(tray_id.label()),
      cwd(std::move(working_directory)),
      pane(std::move(initial_pane)) {
  if (pane == nullptr) {
    throw std::invalid_argument("tray requires a pane");
  }
}

Tray::~Tray() = default;

void Tray::write_input(std::string_view const bytes) const { pane->write_input(bytes); }

std::optional<std::string> Tray::read_output() { return pane->read_output(); }

std::string Tray::redraw_output() const { return pane->redraw_output(); }

std::string Tray::preview_output(TerminalPosition const origin,
                                 base::TerminalSize const region_size) const {
  return pane->preview_output(origin, region_size);
}

void Tray::resize(base::TerminalSize const size) {
  pane->resize(size);
  if (worktree_overlay != nullptr) {
    worktree_overlay->resize(size);
  }
}

std::optional<process::ProcessExitStatus> Tray::try_wait_for_exit() noexcept {
  return pane->try_wait_for_exit();
}

base::FileDescriptor Tray::file_descriptor() const { return pane->file_descriptor(); }

TrayId const& Tray::id() const { return tray_id; }

TraySnapshot Tray::snapshot() const {
  return TraySnapshot{
      .id = tray_id, .label = tray_label, .working_directory = cwd, .child_pid = pane->child_pid()};
}

void Tray::set_worktree_management_overlay(std::unique_ptr<WorktreeManagementOverlay> overlay) {
  if (overlay == nullptr) {
    throw std::invalid_argument("tray worktree overlay must not be null");
  }
  worktree_overlay = std::move(overlay);
}

void Tray::clear_worktree_management_overlay() { worktree_overlay = nullptr; }

WorktreeManagementOverlay* Tray::worktree_management_overlay() noexcept {
  return worktree_overlay.get();
}

WorktreeManagementOverlay const* Tray::worktree_management_overlay() const noexcept {
  return worktree_overlay.get();
}

}  // namespace moe::parent
