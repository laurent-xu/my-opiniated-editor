#include "src/parent/tray_manager.h"

#include <stdexcept>
#include <utility>

namespace moe::parent {

std::unique_ptr<TrayManager> TrayManager::start(TrayConfig config) {
  std::unique_ptr<Tray> first_tray =
      Tray::start(TrayId::anonymous(TrayNumber::one()),
                  TrayConfig{
                      .command = std::move(config.command),
                      .working_directory = std::move(config.working_directory),
                      .initial_size = config.initial_size,
                  });
  return std::unique_ptr<TrayManager>(new TrayManager(std::move(first_tray)));
}

TrayManager::TrayManager(std::unique_ptr<Tray> tray) : active_tray(std::move(tray)) {
  if (active_tray == nullptr) {
    throw std::invalid_argument("tray manager requires an active tray");
  }
}

TrayManager::~TrayManager() = default;

void TrayManager::write_input(std::string_view const bytes) { active_tray->write_input(bytes); }

std::optional<std::string> TrayManager::read_active_output() const {
  return active_tray->read_output();
}

void TrayManager::resize_active(TerminalSize const size) const { active_tray->resize(size); }

std::optional<int> TrayManager::try_wait_for_active_exit() noexcept {
  return active_tray->try_wait_for_exit();
}

base::FileDescriptor TrayManager::active_content_file_descriptor() const {
  return active_tray->file_descriptor();
}

TraySnapshot TrayManager::active_snapshot() const { return active_tray->snapshot(); }

}  // namespace moe::parent
