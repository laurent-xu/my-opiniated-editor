#include "src/parent/worktree/worktree_helper_commands.h"

#include <exception>
#include <optional>
#include <ostream>

#include "src/parent/worktree/repository_registration_request.h"
#include "src/parent/worktree/worktree_provision_request.h"
#include "src/parent/worktree/worktree_provision_result.h"
#include "src/parent/worktree/worktree_provisioner.h"
#include "src/parent/worktree/worktree_repository_registrar.h"

namespace moe::parent {

int run_worktree_repository_registration_command(std::span<char* const> const arguments,
                                                 WorktreeCommandStreams const& streams) {
  if (arguments.size() != 4 && arguments.size() != 5) {
    streams.error_output << "usage: workspace_parent --register-worktree-repository "
                            "<registry-path> <repository-root> [clone-url]\n";
    return 2;
  }

  RepositoryRegistrationRequest request{
      .repository_root = arguments[3],
      .clone_url = std::nullopt,
      .registry_path = arguments[2],
  };
  if (arguments.size() == 5) {
    request.clone_url = arguments[4];
  }

  try {
    WorktreeRepositoryRegistrar(configured_git_executable())
        .register_repository(request, streams.standard_output);
    return 0;
  } catch (std::exception const& error) {
    streams.error_output << "Repository registration failed: " << error.what() << '\n';
    return 1;
  }
}

int run_worktree_provision_command(std::span<char* const> const arguments,
                                   WorktreeCommandStreams const& streams) {
  if (arguments.size() != 6) {
    streams.error_output << "usage: workspace_parent --provision-worktree "
                            "<registry-path> <repository-root> <branch> <worktree-path>\n";
    return 2;
  }

  try {
    static_cast<void>(WorktreeProvisioner(configured_git_executable())
                          .provision(
                              WorktreeProvisionRequest{
                                  .repository_root = arguments[3],
                                  .branch = arguments[4],
                                  .worktree_path = arguments[5],
                                  .registry_path = arguments[2],
                              },
                              streams.standard_output));
    return 0;
  } catch (std::exception const& error) {
    streams.error_output << "Worktree operation failed: " << error.what() << '\n';
    return 1;
  }
}

}  // namespace moe::parent
