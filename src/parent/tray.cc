#include "src/parent/tray.h"

#include <stdexcept>
#include <utility>

#include "src/parent/worktree_management_overlay.h"

namespace moe::parent {

std::unique_ptr<Tray> Tray::start(TrayId id, TrayConfig const& config) {
  std::unique_ptr<ContentPtySession> content =
      ContentPtySession::start(config.command, config.working_directory, config.initial_size);
  return std::unique_ptr<Tray>(
      new Tray(std::move(id), config.working_directory, std::move(content), config.initial_size));
}

Tray::Tray(TrayId id, std::filesystem::path working_directory,
           std::unique_ptr<ContentPtySession> content_pty, TerminalSize const size)
    : tray_id(std::move(id)),
      tray_label(tray_id.label()),
      cwd(std::move(working_directory)),
      content(std::move(content_pty)),
      terminal_screen(size) {
  if (content == nullptr) {
    throw std::invalid_argument("tray requires a content pty");
  }
}

Tray::~Tray() = default;

void Tray::write_input(std::string_view const bytes) const { content->write(bytes); }

std::optional<std::string> Tray::read_output() {
  std::optional<std::string> output = content->read_available();
  if (output.has_value()) {
    terminal_screen.ingest(*output);
  }
  return output;
}

std::string Tray::redraw_output() const { return terminal_screen.render_snapshot(); }

void Tray::resize(TerminalSize const size) {
  content->resize(size);
  terminal_screen.resize(size);
  if (worktree_overlay != nullptr) {
    worktree_overlay->resize(size);
  }
}

std::optional<int> Tray::try_wait_for_exit() noexcept { return content->try_wait_for_exit(); }

base::FileDescriptor Tray::file_descriptor() const { return content->file_descriptor(); }

TrayId const& Tray::id() const { return tray_id; }

TraySnapshot Tray::snapshot() const {
  return TraySnapshot{.id = tray_id,
                      .label = tray_label,
                      .working_directory = cwd,
                      .child_pid = content->child_pid()};
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
