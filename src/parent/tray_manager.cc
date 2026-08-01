#include "src/parent/tray_manager.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "src/parent/worktree_management_overlay.h"

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

std::string preview_target(TrayId const& id) {
  if (id.kind() == TrayIdKind::ANONYMOUS) {
    return "/anonymous/" + std::to_string(id.anonymous_number().value());
  }
  return id.worktree_root().string();
}

std::string render_preview_header(TrayPreviewRequest const& preview) {
  std::size_t const width = static_cast<std::size_t>(std::max(preview.size.cols, 0));
  if (width == 0U || preview.size.rows <= 0) {
    return {};
  }

  constexpr std::string_view PREFIX = "Preview: ";
  std::string const target = preview_target(preview.tray_id);
  std::string title(PREFIX);
  if (title.size() + target.size() <= width) {
    title += target;
  } else if (width > title.size() + 3U) {
    std::size_t const target_width = width - title.size() - 3U;
    title += "..." + target.substr(target.size() - target_width);
  }
  title.resize(width, ' ');

  return "\x1b[?25l\x1b[" + std::to_string(preview.origin.row + 1) + ";" +
         std::to_string(preview.origin.column + 1) + "H\x1b[48;5;236m\x1b[38;5;252m" + title +
         "\x1b[0m";
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

std::optional<std::string> TrayManager::read_output(TrayId const& id) {
  return mutable_tray(id).read_output();
}

std::string TrayManager::active_redraw_output() const { return active_tray().redraw_output(); }

void TrayManager::resize_active(base::TerminalSize const size) {
  current_size = size;
  mutable_active_tray().resize(size);
}

TraySnapshot TrayManager::switch_to(TrayNumber const number) {
  Tray& tray = ensure_tray(number);
  active_tray_id = TrayId::anonymous(number);
  tray.resize(current_size);
  refresh_active_overlay_session_trays();
  return tray.snapshot();
}

TraySnapshot TrayManager::switch_to_worktree(std::filesystem::path const& path) {
  std::filesystem::path const root = worktree_root_for(path);
  Tray& tray = ensure_worktree_tray(root);
  active_tray_id = TrayId::worktree(root);
  tray.resize(current_size);
  refresh_active_overlay_session_trays();
  return tray.snapshot();
}

bool TrayManager::destroy_tray(TrayId const& id) {
  bool destroyed = false;
  if (id.kind() == TrayIdKind::ANONYMOUS) {
    std::unique_ptr<Tray>& tray = anonymous_trays.at(tray_index(id.anonymous_number()));
    destroyed = tray != nullptr;
    tray.reset();
  } else {
    destroyed = worktree_trays.erase(id.worktree_root()) != 0U;
  }

  if (!destroyed) {
    return false;
  }
  if (id == active_tray_id) {
    activate_anonymous_tray_one();
  }
  refresh_active_overlay_session_trays();
  return true;
}

bool TrayManager::destroy_exited_trays() {
  std::vector<TrayId> exited;
  for (std::unique_ptr<Tray> const& tray : anonymous_trays) {
    if (tray != nullptr && tray->try_wait_for_exit().has_value()) {
      exited.push_back(tray->id());
    }
  }
  for (auto const& entry : worktree_trays) {
    if (entry.second->try_wait_for_exit().has_value()) {
      exited.push_back(entry.second->id());
    }
  }

  for (TrayId const& id : exited) {
    static_cast<void>(destroy_tray(id));
  }
  return !exited.empty();
}

base::FileDescriptor TrayManager::active_content_file_descriptor() const {
  return active_tray().file_descriptor();
}

TrayId TrayManager::active_id() const { return active_tray_id; }

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

std::vector<TrayOutputSource> TrayManager::output_sources() const {
  std::vector<TrayOutputSource> sources;
  for (std::unique_ptr<Tray> const& tray : anonymous_trays) {
    if (tray != nullptr) {
      sources.push_back(
          TrayOutputSource{.tray_id = tray->id(), .file_descriptor = tray->file_descriptor()});
    }
  }
  for (auto const& entry : worktree_trays) {
    Tray const& tray = *entry.second;
    sources.push_back(
        TrayOutputSource{.tray_id = tray.id(), .file_descriptor = tray.file_descriptor()});
  }
  return sources;
}

void TrayManager::set_active_worktree_management_overlay(
    std::unique_ptr<WorktreeManagementOverlay> overlay) {
  mutable_active_tray().set_worktree_management_overlay(std::move(overlay));
}

void TrayManager::clear_active_worktree_management_overlay() {
  mutable_active_tray().clear_worktree_management_overlay();
}

void TrayManager::clear_worktree_management_overlay(TrayId const& id) {
  mutable_tray(id).clear_worktree_management_overlay();
}

WorktreeManagementOverlay* TrayManager::active_worktree_management_overlay() {
  return mutable_active_tray().worktree_management_overlay();
}

WorktreeManagementOverlay const* TrayManager::active_worktree_management_overlay() const {
  return active_tray().worktree_management_overlay();
}

WorktreeManagementOverlay* TrayManager::worktree_management_overlay(TrayId const& id) {
  return mutable_tray(id).worktree_management_overlay();
}

std::vector<TrayOutputSource> TrayManager::worktree_management_overlay_output_sources() const {
  std::vector<TrayOutputSource> sources;
  auto const append_source = [&sources](Tray const& tray) {
    WorktreeManagementOverlay const* const overlay = tray.worktree_management_overlay();
    if (overlay == nullptr) {
      return;
    }
    std::optional<base::FileDescriptor> const descriptor = overlay->process_file_descriptor();
    if (descriptor.has_value()) {
      sources.push_back(TrayOutputSource{.tray_id = tray.id(), .file_descriptor = *descriptor});
    }
  };

  for (std::unique_ptr<Tray> const& tray : anonymous_trays) {
    if (tray != nullptr) {
      append_source(*tray);
    }
  }
  for (auto const& entry : worktree_trays) {
    append_source(*entry.second);
  }
  return sources;
}

std::string TrayManager::active_worktree_management_overlay_redraw_output() const {
  WorktreeManagementOverlay const* const overlay = active_worktree_management_overlay();
  if (overlay == nullptr) {
    return {};
  }

  std::string output;
  std::optional<TrayPreviewRequest> const preview = overlay->preview_request();
  if (preview.has_value()) {
    TrayPreviewRequest const content_preview{
        .tray_id = preview->tray_id,
        .origin =
            TerminalPosition{
                .row = preview->origin.row + 1,
                .column = preview->origin.column,
            },
        .size =
            base::TerminalSize{
                .rows = std::max(preview->size.rows - 1, 0),
                .cols = preview->size.cols,
            },
    };
    Tray const* const previewed_tray = find_tray(preview->tray_id);
    output = previewed_tray == nullptr
                 ? TerminalScreen::render_blank_region_snapshot(content_preview.origin,
                                                                content_preview.size)
                 : previewed_tray->preview_output(content_preview.origin, content_preview.size);
    output += render_preview_header(*preview);
  }
  output += overlay->redraw_output();
  return output;
}

bool TrayManager::active_worktree_management_overlay_previews(TrayId const& id) const {
  WorktreeManagementOverlay const* const overlay = active_worktree_management_overlay();
  if (overlay == nullptr) {
    return false;
  }
  std::optional<TrayPreviewRequest> const preview = overlay->preview_request();
  return preview.has_value() && preview->tray_id == id;
}

Tray const& TrayManager::active_tray() const { return tray(active_tray_id); }

Tray& TrayManager::mutable_active_tray() { return mutable_tray(active_tray_id); }

Tray const& TrayManager::tray(TrayId const& id) const {
  if (id.kind() == TrayIdKind::WORKTREE) {
    return worktree_tray(id.worktree_root());
  }

  std::unique_ptr<Tray> const& tray = anonymous_trays.at(tray_index(id.anonymous_number()));
  if (tray == nullptr) {
    throw std::logic_error("requested anonymous tray is missing");
  }
  return *tray;
}

Tray const* TrayManager::find_tray(TrayId const& id) const {
  if (id.kind() == TrayIdKind::WORKTREE) {
    auto const iterator = worktree_trays.find(id.worktree_root());
    return iterator == worktree_trays.end() ? nullptr : iterator->second.get();
  }

  std::unique_ptr<Tray> const& candidate = anonymous_trays.at(tray_index(id.anonymous_number()));
  return candidate.get();
}

Tray& TrayManager::mutable_tray(TrayId const& id) {
  if (id.kind() == TrayIdKind::WORKTREE) {
    return mutable_worktree_tray(id.worktree_root());
  }

  std::unique_ptr<Tray>& tray = anonymous_trays.at(tray_index(id.anonymous_number()));
  if (tray == nullptr) {
    throw std::logic_error("requested anonymous tray is missing");
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
    throw std::logic_error("requested worktree tray is missing");
  }
  return *iterator->second;
}

Tray& TrayManager::mutable_worktree_tray(std::filesystem::path const& root) {
  auto const iterator = worktree_trays.find(root);
  if (iterator == worktree_trays.end()) {
    throw std::logic_error("requested worktree tray is missing");
  }
  return *iterator->second;
}

void TrayManager::refresh_active_overlay_session_trays() {
  WorktreeManagementOverlay* const overlay = active_worktree_management_overlay();
  if (overlay != nullptr) {
    overlay->update_session_trays(tray_snapshots());
  }
}

void TrayManager::activate_anonymous_tray_one() {
  active_tray_id = TrayId::anonymous(TrayNumber::one());
  Tray& tray = ensure_tray(TrayNumber::one());
  tray.resize(current_size);
}

std::size_t TrayManager::tray_index(TrayNumber const number) {
  return static_cast<std::size_t>(number.value() - TrayNumber::MIN_VALUE);
}

}  // namespace moe::parent
