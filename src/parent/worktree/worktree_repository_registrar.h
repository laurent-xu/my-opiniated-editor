#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace moe::parent {

enum class RepositoryRootState : std::uint8_t {
  EMPTY,
  BARE_ROOT,
  RECOVERABLE_BARE_ROOT,
};

struct RepositoryRegistrationRequest {
  std::filesystem::path repository_root;
  std::optional<std::string> clone_url;
  std::filesystem::path registry_path;
};

[[nodiscard]] RepositoryRootState inspect_repository_root(
    std::filesystem::path const& repository_root);
[[nodiscard]] std::string configured_git_executable();

class WorktreeRepositoryRegistrar {
 public:
  explicit WorktreeRepositoryRegistrar(std::string git_executable);

  void register_repository(RepositoryRegistrationRequest const& request,
                           std::ostream& progress) const;

 private:
  std::string git_executable;
};

}  // namespace moe::parent
