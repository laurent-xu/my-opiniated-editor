#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "src/parent/test/support/test_paths.h"
#include "src/parent/worktree/registry/worktree_registry_store.h"

namespace moe::parent::test_support {

inline std::filesystem::path test_registry_path(std::string const& name) {
  std::filesystem::path const directory = required_environment_path("TEST_TMPDIR") / name;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory / "worktrees.pb";
}

inline void write_bytes(std::filesystem::path const& path, std::string const& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("failed to open test file");
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed to write test file");
  }
}

inline std::string read_bytes(std::filesystem::path const& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open test file");
  }
  return {std::istreambuf_iterator<char>(input), {}};
}

inline persistence::Repository* add_repository(
    persistence::WorktreeRegistry& registry, std::filesystem::path const& root,
    std::vector<std::filesystem::path> const& worktrees) {
  persistence::Repository* repository = registry.add_repositories();
  repository->set_root_path(root.string());
  for (std::filesystem::path const& worktree : worktrees) {
    repository->add_worktrees()->set_path(worktree.string());
  }
  return repository;
}

}  // namespace moe::parent::test_support
