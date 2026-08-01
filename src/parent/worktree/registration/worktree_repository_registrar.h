#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace moe::parent {

enum class RepositoryRootState : std::uint8_t;
struct RepositoryRegistrationRequest;

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
