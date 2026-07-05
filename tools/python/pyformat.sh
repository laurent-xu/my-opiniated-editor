#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${BUILD_WORKING_DIRECTORY:-}" ]]; then
  cd "$BUILD_WORKING_DIRECTORY"
fi

runfiles="${RUNFILES_DIR:-$0.runfiles}"
ruff="$(find "$runfiles" -path '*nixpkgs_ruff/bin/ruff' -print -quit)"

if [[ -z "$ruff" ]]; then
  echo "could not find Nix-provided ruff in Bazel runfiles" >&2
  exit 1
fi

exec "$ruff" format "$@"
