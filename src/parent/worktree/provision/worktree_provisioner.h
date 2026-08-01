#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

namespace moe::parent {

struct WorktreeProvisionRequest;
struct WorktreeProvisionResult;

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
