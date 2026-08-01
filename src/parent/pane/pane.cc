#include "src/parent/pane/pane.h"

#include <stdexcept>
#include <utility>

#include "src/parent/terminal/content_pty_session.h"
#include "src/parent/terminal/screen/terminal_screen.h"

namespace moe::parent {

std::unique_ptr<Pane> Pane::start(PaneConfig const& config) {
  std::unique_ptr<ContentPtySession> content =
      ContentPtySession::start(config.command, config.working_directory, config.initial_size);
  return std::unique_ptr<Pane>(new Pane(std::move(content), config.initial_size));
}

Pane::Pane(std::unique_ptr<ContentPtySession> content_pty, base::TerminalSize const size)
    : content(std::move(content_pty)), terminal_screen(std::make_unique<TerminalScreen>(size)) {
  if (content == nullptr) {
    throw std::invalid_argument("pane requires a content pty");
  }
}

Pane::~Pane() = default;

void Pane::write_input(std::string_view const bytes) const { content->write(bytes); }

std::optional<std::string> Pane::read_output() {
  std::optional<std::string> output = content->read_available();
  if (output.has_value()) {
    terminal_screen->ingest(*output);
  }
  return output;
}

std::string Pane::redraw_output() const { return terminal_screen->render_snapshot(); }

std::string Pane::redraw_output_at(TerminalPosition const origin) const {
  return terminal_screen->render_region_snapshot(origin);
}

std::string Pane::preview_output(TerminalPosition const origin,
                                 base::TerminalSize const region_size) const {
  return terminal_screen->render_region_snapshot(origin, region_size);
}

void Pane::resize(base::TerminalSize const size) {
  content->resize(size);
  terminal_screen->resize(size);
}

std::optional<process::ProcessExitStatus> Pane::try_wait_for_exit() noexcept {
  return content->try_wait_for_exit();
}

base::FileDescriptor Pane::file_descriptor() const { return content->file_descriptor(); }

base::ProcessId Pane::child_pid() const { return content->child_pid(); }

}  // namespace moe::parent
