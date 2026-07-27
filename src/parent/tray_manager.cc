#include "src/parent/tray_manager.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace moe::parent {

std::unique_ptr<TrayManager> TrayManager::start(TrayConfig config) {
  return std::unique_ptr<TrayManager>(new TrayManager(std::move(config)));
}

TrayManager::TrayManager(TrayConfig tray_config)
    : config(std::move(tray_config)),
      current_size(config.initial_size),
      active_tray_number(TrayNumber::one()) {
  if (config.command.empty()) {
    throw std::invalid_argument("tray manager command must not be empty");
  }
  static_cast<void>(ensure_tray(active_tray_number));
}

TrayManager::~TrayManager() = default;

void TrayManager::write_input(std::string_view const bytes) { active_tray().write_input(bytes); }

std::optional<std::string> TrayManager::read_active_output() const {
  return active_tray().read_output();
}

void TrayManager::resize_active(TerminalSize const size) {
  current_size = size;
  active_tray().resize(size);
}

TraySnapshot TrayManager::switch_to(TrayNumber const number) {
  Tray& tray = ensure_tray(number);
  active_tray_number = number;
  tray.resize(current_size);
  return tray.snapshot();
}

std::optional<int> TrayManager::try_wait_for_active_exit() noexcept {
  return mutable_active_tray().try_wait_for_exit();
}

base::FileDescriptor TrayManager::active_content_file_descriptor() const {
  return active_tray().file_descriptor();
}

TraySnapshot TrayManager::active_snapshot() const { return active_tray().snapshot(); }

std::vector<TraySnapshot> TrayManager::tray_snapshots() const {
  std::vector<TraySnapshot> snapshots;
  for (std::unique_ptr<Tray> const& tray : anonymous_trays) {
    if (tray != nullptr) {
      snapshots.push_back(tray->snapshot());
    }
  }
  return snapshots;
}

Tray const& TrayManager::active_tray() const {
  std::unique_ptr<Tray> const& tray = anonymous_trays.at(tray_index(active_tray_number));
  if (tray == nullptr) {
    throw std::logic_error("active tray is missing");
  }
  return *tray;
}

Tray& TrayManager::mutable_active_tray() {
  std::unique_ptr<Tray>& tray = anonymous_trays.at(tray_index(active_tray_number));
  if (tray == nullptr) {
    throw std::logic_error("active tray is missing");
  }
  return *tray;
}

Tray& TrayManager::ensure_tray(TrayNumber const number) {
  std::unique_ptr<Tray>& tray = anonymous_trays.at(tray_index(number));
  if (tray == nullptr) {
    tray = Tray::start(TrayId::anonymous(number), TrayConfig{
                                                      .command = config.command,
                                                      .working_directory = config.working_directory,
                                                      .initial_size = current_size,
                                                  });
  }
  return *tray;
}

std::size_t TrayManager::tray_index(TrayNumber const number) {
  return static_cast<std::size_t>(number.value() - TrayNumber::MIN_VALUE);
}

}  // namespace moe::parent
