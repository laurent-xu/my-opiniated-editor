#include <cstddef>
#include <span>

#include "src/parent/workspace_parent.h"

int main(int const argument_count, char* arguments[]) {
  return moe::parent::run_workspace_parent_command(
      std::span<char*>(arguments, static_cast<std::size_t>(argument_count)));
}
