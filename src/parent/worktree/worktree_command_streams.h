#pragma once

#include <iosfwd>

namespace moe::parent {

struct WorktreeCommandStreams {
  std::ostream& standard_output;
  std::ostream& error_output;
};

}  // namespace moe::parent
