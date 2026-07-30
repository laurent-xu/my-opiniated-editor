#include "src/parent/tray_id.h"

#include <stdexcept>
#include <utility>

namespace moe::parent {

TrayId TrayId::anonymous(TrayNumber const number) { return TrayId(number); }

TrayId TrayId::worktree(std::filesystem::path root) { return TrayId(std::move(root)); }

TrayIdKind TrayId::kind() const {
  if (std::holds_alternative<TrayNumber>(value)) {
    return TrayIdKind::ANONYMOUS;
  }
  return TrayIdKind::WORKTREE;
}

TrayNumber TrayId::anonymous_number() const {
  if (!std::holds_alternative<TrayNumber>(value)) {
    throw std::logic_error("tray id is not anonymous");
  }
  return std::get<TrayNumber>(value);
}

std::filesystem::path const& TrayId::worktree_root() const {
  if (!std::holds_alternative<std::filesystem::path>(value)) {
    throw std::logic_error("tray id is not a worktree");
  }
  return std::get<std::filesystem::path>(value);
}

std::string TrayId::key() const {
  if (kind() == TrayIdKind::ANONYMOUS) {
    return "anonymous:" + std::to_string(anonymous_number().value());
  }
  return "worktree:" + worktree_root().string();
}

std::string TrayId::label() const {
  if (kind() == TrayIdKind::ANONYMOUS) {
    return "tray " + std::to_string(anonymous_number().value());
  }
  return "worktree " + worktree_root().string();
}

TrayId::TrayId(TrayNumber const tray_number) : value(tray_number) {}

TrayId::TrayId(std::filesystem::path root) : value(std::move(root)) {}

}  // namespace moe::parent
