#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace moe::parent {

class WorktreeCandidateFinder {
 public:
  explicit WorktreeCandidateFinder(std::string git_executable);

  [[nodiscard]] std::vector<std::filesystem::path> find_available(
      std::filesystem::path const& registry_path) const;

 private:
  std::string git_executable;
};

}  // namespace moe::parent
