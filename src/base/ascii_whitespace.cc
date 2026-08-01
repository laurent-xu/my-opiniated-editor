#include "src/base/ascii_whitespace.h"

#include <cstddef>
#include <string>

namespace moe::base {
namespace {

constexpr bool is_trimmable_ascii_whitespace(char const character) {
  return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

}  // namespace

std::string trim_ascii_whitespace(std::string value) {
  while (!value.empty() && is_trimmable_ascii_whitespace(value.back())) {
    value.pop_back();
  }
  std::size_t start = 0;
  while (start < value.size() && is_trimmable_ascii_whitespace(value[start])) {
    ++start;
  }
  return value.substr(start);
}

}  // namespace moe::base
