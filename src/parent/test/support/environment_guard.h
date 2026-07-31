#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace moe::parent::test_support {

class EnvironmentGuard {
 public:
  explicit EnvironmentGuard(std::string name)
      : name(std::move(name)), original(read_value(this->name)) {}

  EnvironmentGuard(EnvironmentGuard const&) = delete;
  EnvironmentGuard& operator=(EnvironmentGuard const&) = delete;

  ~EnvironmentGuard() {
    if (original.has_value()) {
      static_cast<void>(::setenv(name.c_str(), original->c_str(), 1));
    } else {
      static_cast<void>(::unsetenv(name.c_str()));
    }
  }

 private:
  static std::optional<std::string> read_value(std::string const& name) {
    char const* const value = std::getenv(name.c_str());
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
  }

  std::string name;
  std::optional<std::string> original;
};

}  // namespace moe::parent::test_support
