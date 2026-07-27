#include "src/parent/tray_manager.h"

#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace moe::parent {
namespace {

bool has_git_marker(std::filesystem::path const& path) {
  std::error_code error;
  return std::filesystem::exists(path / ".git", error);
}

std::filesystem::path canonical_path(std::filesystem::path const& path) {
  std::error_code error;
  std::filesystem::path const canonical = std::filesystem::weakly_canonical(path, error);
  if (error != std::error_code{}) {
    throw std::invalid_argument("failed to resolve path: " + path.string());
  }
  return canonical;
}

std::filesystem::path worktree_root_for(std::filesystem::path const& path) {
  if (path.empty()) {
    throw std::invalid_argument("worktree path must not be empty");
  }

  std::filesystem::path current = canonical_path(path);
  if (!std::filesystem::is_directory(current)) {
    current = current.parent_path();
  }

  while (!current.empty()) {
    if (has_git_marker(current)) {
      return current;
    }
    if (current == current.root_path()) {
      break;
    }
    current = current.parent_path();
  }

  throw std::invalid_argument("path is not inside a git worktree: " + path.string());
}

}  // namespace

std::unique_ptr<TrayManager> TrayManager::start(TrayConfig config) {
  return std::unique_ptr<TrayManager>(new TrayManager(std::move(config)));
}

TrayManager::TrayManager(TrayConfig tray_config)
    : config(std::move(tray_config)),
      current_size(config.initial_size),
      active_tray_id(TrayId::anonymous(TrayNumber::one())) {
  if (config.command.empty()) {
    throw std::invalid_argument("tray manager command must not be empty");
  }
  static_cast<void>(ensure_tray(active_tray_id.anonymous_number()));
}

TrayManager::~TrayManager() = default;

void TrayManager::write_input(std::string_view const bytes) { active_tray().write_input(bytes); }

std::optional<std::string> TrayManager::read_active_output() {
  return mutable_active_tray().read_output();
}

std::string_view TrayManager::active_replay_output() const { return active_tray().replay_output(); }

void TrayManager::resize_active(TerminalSize const size) {
  current_size = size;
  active_tray().resize(size);
}

TraySnapshot TrayManager::switch_to(TrayNumber const number) {
  Tray& tray = ensure_tray(number);
  active_tray_id = TrayId::anonymous(number);
  tray.resize(current_size);
  return tray.snapshot();
}

TraySnapshot TrayManager::switch_to_worktree(std::filesystem::path const& path) {
  std::filesystem::path const root = worktree_root_for(path);
  Tray& tray = ensure_worktree_tray(root);
  active_tray_id = TrayId::worktree(root);
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
  for (auto const& entry : worktree_trays) {
    snapshots.push_back(entry.second->snapshot());
  }
  return snapshots;
}

Tray const& TrayManager::active_tray() const {
  if (active_tray_id.kind() == TrayIdKind::WORKTREE) {
    return worktree_tray(active_tray_id.worktree_root());
  }

  std::unique_ptr<Tray> const& tray =
      anonymous_trays.at(tray_index(active_tray_id.anonymous_number()));
  if (tray == nullptr) {
    throw std::logic_error("active tray is missing");
  }
  return *tray;
}

Tray& TrayManager::mutable_active_tray() {
  if (active_tray_id.kind() == TrayIdKind::WORKTREE) {
    return ensure_worktree_tray(active_tray_id.worktree_root());
  }

  std::unique_ptr<Tray>& tray = anonymous_trays.at(tray_index(active_tray_id.anonymous_number()));
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

Tray& TrayManager::ensure_worktree_tray(std::filesystem::path const& root) {
  auto iterator = worktree_trays.find(root);
  if (iterator == worktree_trays.end()) {
    iterator = worktree_trays
                   .emplace(root, Tray::start(TrayId::worktree(root),
                                              TrayConfig{
                                                  .command = config.command,
                                                  .working_directory = root,
                                                  .initial_size = current_size,
                                              }))
                   .first;
  }
  return *iterator->second;
}

Tray const& TrayManager::worktree_tray(std::filesystem::path const& root) const {
  auto const iterator = worktree_trays.find(root);
  if (iterator == worktree_trays.end()) {
    throw std::logic_error("active worktree tray is missing");
  }
  return *iterator->second;
}

std::size_t TrayManager::tray_index(TrayNumber const number) {
  return static_cast<std::size_t>(number.value() - TrayNumber::MIN_VALUE);
}

}  // namespace moe::parent
