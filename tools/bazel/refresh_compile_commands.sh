#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

# Do not use `bazel run //:refresh_compile_commands` here. The generated
# Hedron script calls Bazel internally, and an outer `bazel run` keeps the
# Bazel client lock held while the script waits for that nested Bazel command.
bazel --batch build //:refresh_compile_commands

BUILD_WORKSPACE_DIRECTORY="$repo_root" \
BUILD_WORKING_DIRECTORY="$repo_root" \
  "$repo_root/bazel-bin/refresh_compile_commands"
