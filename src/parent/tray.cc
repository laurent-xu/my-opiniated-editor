#include "src/parent/tray.h"

#include <stdexcept>
#include <utility>

namespace moe::parent {
namespace {

constexpr std::size_t MAX_REPLAY_BUFFER_BYTES = static_cast<std::size_t>(256U) * 1024U;

void append_to_replay_buffer(std::string& replay_buffer, std::string_view const output) {
  replay_buffer.append(output);
  if (replay_buffer.size() > MAX_REPLAY_BUFFER_BYTES) {
    replay_buffer.erase(0, replay_buffer.size() - MAX_REPLAY_BUFFER_BYTES);
  }
}

}  // namespace

std::unique_ptr<Tray> Tray::start(TrayId id, TrayConfig const& config) {
  std::unique_ptr<ContentPtySession> content =
      ContentPtySession::start(config.command, config.working_directory, config.initial_size);
  return std::unique_ptr<Tray>(new Tray(id, config.working_directory, std::move(content)));
}

Tray::Tray(TrayId id, std::filesystem::path working_directory,
           std::unique_ptr<ContentPtySession> content_pty)
    : tray_id(id),
      tray_label(tray_id.label()),
      cwd(std::move(working_directory)),
      content(std::move(content_pty)) {
  if (content == nullptr) {
    throw std::invalid_argument("tray requires a content pty");
  }
}

Tray::~Tray() = default;

void Tray::write_input(std::string_view const bytes) const { content->write(bytes); }

std::optional<std::string> Tray::read_output() {
  std::optional<std::string> output = content->read_available();
  if (output.has_value()) {
    append_to_replay_buffer(output_replay_buffer, *output);
  }
  return output;
}

std::string_view Tray::replay_output() const { return output_replay_buffer; }

void Tray::resize(TerminalSize const size) const { content->resize(size); }

std::optional<int> Tray::try_wait_for_exit() noexcept { return content->try_wait_for_exit(); }

base::FileDescriptor Tray::file_descriptor() const { return content->file_descriptor(); }

TraySnapshot Tray::snapshot() const {
  return TraySnapshot{.id = tray_id,
                      .label = tray_label,
                      .working_directory = cwd,
                      .child_pid = content->child_pid()};
}

}  // namespace moe::parent
