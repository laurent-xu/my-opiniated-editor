#pragma once

#include <filesystem>
#include <string>
#include <variant>

#include "src/parent/tray/tray_id_kind.h"
#include "src/parent/tray/tray_number.h"

namespace moe::parent {

class TrayId {
 public:
  [[nodiscard]] static TrayId anonymous(TrayNumber number);
  [[nodiscard]] static TrayId worktree(std::filesystem::path root);

  [[nodiscard]] TrayIdKind kind() const;
  [[nodiscard]] TrayNumber anonymous_number() const;
  [[nodiscard]] std::filesystem::path const& worktree_root() const;
  [[nodiscard]] std::string key() const;
  [[nodiscard]] std::string label() const;

  [[nodiscard]] bool operator==(TrayId const& other) const { return value == other.value; }
  [[nodiscard]] bool operator!=(TrayId const& other) const { return !(*this == other); }

 private:
  explicit TrayId(TrayNumber number);
  explicit TrayId(std::filesystem::path root);

  std::variant<TrayNumber, std::filesystem::path> value;
};

}  // namespace moe::parent
