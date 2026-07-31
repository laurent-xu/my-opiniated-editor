#pragma once

#include <string>

#include "src/parent/worktree_removal_request.h"

namespace moe::parent {

class WorktreeRemover {
 public:
  explicit WorktreeRemover(std::string git_executable);

  void remove(WorktreeRemovalRequest const& request) const;

 private:
  std::string git_executable;
};

}  // namespace moe::parent
