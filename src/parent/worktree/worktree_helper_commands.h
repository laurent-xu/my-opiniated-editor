#pragma once

#include <span>

#include "src/parent/worktree/worktree_command_streams.h"

namespace moe::parent {

int run_worktree_repository_registration_command(std::span<char* const> arguments,
                                                 WorktreeCommandStreams const& streams);
int run_worktree_provision_command(std::span<char* const> arguments,
                                   WorktreeCommandStreams const& streams);

}  // namespace moe::parent
