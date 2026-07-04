#include "src/parent/workspace_parent.h"

#include <string>

#include "src/core/version.h"

namespace moe::parent {

std::string StartupBanner() {
  return std::string(moe::core::ProjectName()) + " " +
         std::string(moe::core::PhaseName());
}

}  // namespace moe::parent

