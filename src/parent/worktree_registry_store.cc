#include "src/parent/worktree_registry_store.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "src/base/file_descriptor.h"
#include "src/base/owned_file_descriptor.h"

namespace moe::parent {
namespace {

constexpr char const* STATE_DIRECTORY_ENVIRONMENT = "MOE_STATE_DIRECTORY";

struct NormalizedRepository {
  std::string root_path;
  std::vector<std::string> worktree_paths;
};

[[noreturn]] void throw_errno(std::string const& operation) {
  int const error = errno;
  throw std::system_error(error, std::generic_category(), operation);
}

std::filesystem::path absolute_path(std::filesystem::path const& path,
                                    std::string const& field_name) {
  if (path.empty()) {
    throw std::invalid_argument(field_name + " must not be empty");
  }
  if (!path.is_absolute()) {
    throw std::invalid_argument(field_name + " must be absolute: " + path.string());
  }

  std::error_code error;
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
  if (error != std::error_code{}) {
    throw std::invalid_argument("failed to normalize " + field_name + ": " + path.string());
  }
  while (normalized != normalized.root_path() && normalized.filename().empty()) {
    normalized = normalized.parent_path();
  }
  return normalized;
}

persistence::WorktreeRegistry normalize_registry(persistence::WorktreeRegistry const& registry) {
  if (registry.format_version() != WorktreeRegistryStore::FORMAT_VERSION) {
    throw std::invalid_argument("unsupported worktree registry format version: " +
                                std::to_string(registry.format_version()));
  }

  std::set<std::string> repository_roots;
  std::set<std::string> worktree_paths;
  std::vector<NormalizedRepository> repositories;
  repositories.reserve(static_cast<std::size_t>(registry.repositories_size()));

  for (persistence::Repository const& repository : registry.repositories()) {
    std::string const root_path = absolute_path(repository.root_path(), "repository root").string();
    if (!repository_roots.insert(root_path).second) {
      throw std::invalid_argument("duplicate repository root: " + root_path);
    }

    std::vector<std::string> repository_worktree_paths;
    repository_worktree_paths.reserve(static_cast<std::size_t>(repository.worktrees_size()));
    for (persistence::Worktree const& worktree : repository.worktrees()) {
      std::string const worktree_path = absolute_path(worktree.path(), "worktree path").string();
      if (!worktree_paths.insert(worktree_path).second) {
        throw std::invalid_argument("duplicate worktree path: " + worktree_path);
      }
      repository_worktree_paths.push_back(worktree_path);
    }
    std::ranges::sort(repository_worktree_paths);

    repositories.push_back(NormalizedRepository{
        .root_path = root_path,
        .worktree_paths = std::move(repository_worktree_paths),
    });
  }

  std::ranges::sort(repositories, {}, &NormalizedRepository::root_path);

  persistence::WorktreeRegistry normalized;
  normalized.set_format_version(WorktreeRegistryStore::FORMAT_VERSION);
  for (NormalizedRepository const& repository : repositories) {
    persistence::Repository* output_repository = normalized.add_repositories();
    output_repository->set_root_path(repository.root_path);
    for (std::string const& worktree_path : repository.worktree_paths) {
      output_repository->add_worktrees()->set_path(worktree_path);
    }
  }
  return normalized;
}

void write_all(base::FileDescriptor const descriptor, std::string const& bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    ssize_t const written =
        ::write(descriptor.value(), bytes.data() + offset, bytes.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw_errno("write worktree registry temporary file");
    }
    if (written == 0) {
      throw std::runtime_error("write worktree registry temporary file returned zero");
    }
    offset += static_cast<std::size_t>(written);
  }
}

void sync_file(base::FileDescriptor const descriptor, std::string const& description) {
  if (::fsync(descriptor.value()) < 0) {
    throw_errno("fsync " + description);
  }
}

std::filesystem::path create_temporary_file(std::filesystem::path const& registry_path,
                                            base::OwnedFileDescriptor& descriptor) {
  std::filesystem::path const template_path =
      registry_path.parent_path() / (registry_path.filename().string() + ".tmp.XXXXXX");
  std::string path_buffer = template_path.string();
  path_buffer.push_back('\0');

  int const raw_descriptor = ::mkstemp(path_buffer.data());
  if (raw_descriptor < 0) {
    throw_errno("create worktree registry temporary file");
  }

  descriptor.reset(base::FileDescriptor(raw_descriptor));
  return {path_buffer.data()};
}

base::OwnedFileDescriptor open_directory(std::filesystem::path const& path) {
  int const raw_descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (raw_descriptor < 0) {
    throw_errno("open worktree registry directory");
  }
  return base::OwnedFileDescriptor(base::FileDescriptor(raw_descriptor));
}

bool path_exists(std::filesystem::path const& path) {
  std::error_code error;
  bool const exists = std::filesystem::exists(path, error);
  if (error != std::error_code{}) {
    throw std::filesystem::filesystem_error("inspect worktree registry", path, error);
  }
  return exists;
}

}  // namespace

WorktreeRegistryStore::WorktreeRegistryStore(std::filesystem::path path)
    : registry_path(std::move(path)) {
  if (registry_path.empty()) {
    throw std::invalid_argument("worktree registry path must not be empty");
  }

  std::error_code error;
  std::filesystem::path const absolute_registry_path =
      std::filesystem::absolute(registry_path, error);
  if (error != std::error_code{}) {
    throw std::invalid_argument("failed to resolve worktree registry path");
  }
  registry_path = absolute_registry_path.lexically_normal();
}

std::filesystem::path WorktreeRegistryStore::default_registry_path() {
  char const* state_directory = std::getenv(STATE_DIRECTORY_ENVIRONMENT);
  if (state_directory != nullptr && state_directory[0] != '\0') {
    std::filesystem::path const state_path(state_directory);
    if (!state_path.is_absolute()) {
      throw std::runtime_error("MOE_STATE_DIRECTORY must be an absolute path");
    }
    return state_path / "worktrees.pb";
  }

  char const* xdg_state_home = std::getenv("XDG_STATE_HOME");
  if (xdg_state_home != nullptr && xdg_state_home[0] != '\0') {
    std::filesystem::path const state_home(xdg_state_home);
    if (!state_home.is_absolute()) {
      throw std::runtime_error("XDG_STATE_HOME must be an absolute path");
    }
    return state_home / "my-opiniated-editor" / "worktrees.pb";
  }

  char const* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    throw std::runtime_error("HOME is required when XDG_STATE_HOME is unset");
  }
  std::filesystem::path const home_path(home);
  if (!home_path.is_absolute()) {
    throw std::runtime_error("HOME must be an absolute path");
  }
  return home_path / ".local" / "state" / "my-opiniated-editor" / "worktrees.pb";
}

persistence::WorktreeRegistry WorktreeRegistryStore::empty_registry() {
  persistence::WorktreeRegistry registry;
  registry.set_format_version(FORMAT_VERSION);
  return registry;
}

std::filesystem::path const& WorktreeRegistryStore::path() const noexcept { return registry_path; }

persistence::WorktreeRegistry WorktreeRegistryStore::load() const {
  if (!path_exists(registry_path)) {
    return empty_registry();
  }

  std::ifstream input(registry_path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open worktree registry: " + registry_path.string());
  }
  std::string const bytes(std::istreambuf_iterator<char>(input), {});
  if (input.bad()) {
    throw std::runtime_error("failed to read worktree registry: " + registry_path.string());
  }

  persistence::WorktreeRegistry registry;
  if (!registry.ParseFromString(bytes)) {
    throw std::runtime_error("failed to parse worktree registry: " + registry_path.string());
  }
  try {
    return normalize_registry(registry);
  } catch (std::invalid_argument const& error) {
    throw std::runtime_error("invalid worktree registry " + registry_path.string() + ": " +
                             error.what());
  }
}

void WorktreeRegistryStore::save(persistence::WorktreeRegistry const& registry) const {
  if (path_exists(registry_path)) {
    static_cast<void>(load());
  }

  persistence::WorktreeRegistry const normalized = normalize_registry(registry);
  std::string serialized;
  if (!normalized.SerializeToString(&serialized)) {
    throw std::runtime_error("failed to serialize worktree registry");
  }

  std::error_code directory_error;
  std::filesystem::create_directories(registry_path.parent_path(), directory_error);
  if (directory_error != std::error_code{}) {
    throw std::filesystem::filesystem_error("create worktree registry directory",
                                            registry_path.parent_path(), directory_error);
  }

  base::OwnedFileDescriptor directory = open_directory(registry_path.parent_path());
  std::filesystem::path temporary_path;
  bool renamed = false;
  try {
    {
      base::OwnedFileDescriptor temporary_descriptor;
      temporary_path = create_temporary_file(registry_path, temporary_descriptor);
      write_all(temporary_descriptor.get(), serialized);
      sync_file(temporary_descriptor.get(), "worktree registry temporary file");
    }

    if (::rename(temporary_path.c_str(), registry_path.c_str()) < 0) {
      throw_errno("replace worktree registry");
    }
    renamed = true;
    sync_file(directory.get(), "worktree registry directory");
  } catch (...) {
    if (!renamed && !temporary_path.empty()) {
      std::error_code remove_error;
      std::filesystem::remove(temporary_path, remove_error);
    }
    throw;
  }
}

}  // namespace moe::parent
