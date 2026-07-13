#!/usr/bin/env bash
set -euo pipefail

# Run the parent PTY browser bridge.
# systemd loads ~/.config/my-opiniated-editor/bridge.env before ExecStart.

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

: "${MOE_BRIDGE_INTERFACE:?MOE_BRIDGE_INTERFACE is required}"
: "${MOE_BRIDGE_PORT:?MOE_BRIDGE_PORT is required}"
: "${MOE_BRIDGE_TOKEN:?MOE_BRIDGE_TOKEN is required}"

cd "${repo_root}"

bazel --batch build \
  //src/bridge:parent_ws_bridge \
  //src/parent:workspace_parent

exec bazel-bin/src/bridge/parent_ws_bridge \
  --interface "${MOE_BRIDGE_INTERFACE}" \
  --port "${MOE_BRIDGE_PORT}" \
  --token "${MOE_BRIDGE_TOKEN}" \
  --parent bazel-bin/src/parent/workspace_parent \
  --cwd "${repo_root}"
