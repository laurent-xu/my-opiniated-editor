#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace moe::parent {

enum class WorktreeProvisionKind : std::uint8_t {
  ADOPTED,
  CREATED,
};

struct WorktreeProvisionRequest {
  std::filesystem::path repository_root;
  std::string branch;
  std::filesystem::path worktree_path;
  std::filesystem::path registry_path;
};

struct WorktreeProvisionResult {
  std::filesystem::path worktree_path;
  WorktreeProvisionKind kind;
};

[[nodiscard]] std::filesystem::path derived_worktree_path(
    std::filesystem::path const& repository_root, std::string const& branch);

class WorktreeProvisioner {
 public:
  explicit WorktreeProvisioner(std::string git_executable);

  [[nodiscard]] WorktreeProvisionResult provision(WorktreeProvisionRequest const& request,
                                                  std::ostream& progress) const;

 private:
  std::string git_executable;
};

}  // namespace moe::parent
