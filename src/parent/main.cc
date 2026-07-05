#include <unistd.h>

#include <iostream>

#include "src/parent/workspace_parent.h"

int main() {
  if (::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0) {
    return moe::parent::run_interactive(std::cin, std::cout);
  }

  std::cout << moe::parent::startup_banner() << '\n';
  return 0;
}
