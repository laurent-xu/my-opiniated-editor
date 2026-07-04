#include "src/parent/workspace_parent.h"

#include <string>

#include "src/core/version.h"

namespace moe::parent {

std::string startup_banner() {
  return std::string(moe::core::project_name()) + " " + std::string(moe::core::phase_name());
}

}  // namespace moe::parent
