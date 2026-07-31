#pragma once

#include <cstdint>
#include <filesystem>

#include "src/parent/worktree/worktree_registry.pb.h"

namespace moe::parent {

class WorktreeRegistryStore {
 public:
  static constexpr std::uint32_t FORMAT_VERSION = 1;

  explicit WorktreeRegistryStore(std::filesystem::path registry_path);

  [[nodiscard]] static std::filesystem::path default_registry_path();
  [[nodiscard]] static persistence::WorktreeRegistry empty_registry();

  [[nodiscard]] std::filesystem::path const& path() const noexcept;
  [[nodiscard]] persistence::WorktreeRegistry load() const;
  void save(persistence::WorktreeRegistry const& registry) const;

 private:
  std::filesystem::path registry_path;
};

}  // namespace moe::parent
