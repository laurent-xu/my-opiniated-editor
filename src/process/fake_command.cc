#include <csignal>
#include <iostream>
#include <string_view>

int main(int const argc, char** argv) {
  if (argc != 2) {
    return 64;
  }

  std::string_view const action(argv[1]);
  if (action == "success") {
    return 0;
  }
  if (action == "stdout") {
    std::cout << "captured output\n";
    return 0;
  }
  if (action == "nonzero") {
    return 23;
  }
  if (action == "signal") {
    std::raise(SIGTERM);
    return 70;
  }
  return 65;
}
