#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace moe::parent::test_support {

inline std::filesystem::path required_environment_path(char const* const name) {
  char const* const value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    throw std::runtime_error("missing test environment variable: " + std::string(name));
  }
  return value;
}

inline std::filesystem::path runfile_path(std::filesystem::path const& path) {
  return required_environment_path("TEST_SRCDIR") / required_environment_path("TEST_WORKSPACE") /
         path;
}

}  // namespace moe::parent::test_support
