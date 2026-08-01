#include <unistd.h>

#include <cstddef>
#include <string_view>

namespace {

void write_all(std::string_view output) {
  while (!output.empty()) {
    ssize_t const count = ::write(STDOUT_FILENO, output.data(), output.size());
    if (count <= 0) {
      _exit(125);
    }
    output.remove_prefix(static_cast<std::size_t>(count));
  }
}

}  // namespace

int main() {
  write_all("plain \x1b[31mred\x1b[0m\r\n");
  write_all("caf\xC3\xA9\b!\r\n");
  write_all("partial");
  return 7;
}
