#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace moe::parent {

struct RepositoryRegistrationRequest {
  std::filesystem::path repository_root;
  std::optional<std::string> clone_url;
  std::filesystem::path registry_path;
};

}  // namespace moe::parent
