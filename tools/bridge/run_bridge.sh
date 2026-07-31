#!/usr/bin/env bash
set -euo pipefail

# Run the already-built parent PTY browser bridge.
# systemd loads ~/.config/my-opiniated-editor/bridge.env before ExecStart.

if [[ "$#" -ne 1 || ! "$1" =~ ^[0-9]{1,5}$ ]]; then
  echo "usage: $0 <port>" >&2
  exit 2
fi
port="$((10#$1))"
if ((port < 1 || port > 65535)); then
  echo "port must be between 1 and 65535" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

: "${MOE_BRIDGE_INTERFACE:?MOE_BRIDGE_INTERFACE is required}"
: "${MOE_BRIDGE_TOKEN:?MOE_BRIDGE_TOKEN is required}"

if [[ -n "${XDG_STATE_HOME:-}" ]]; then
  state_root="${XDG_STATE_HOME}"
else
  : "${HOME:?HOME is required when XDG_STATE_HOME is unset}"
  state_root="${HOME}/.local/state"
fi
state_directory="${state_root}/my-opiniated-editor/instances/port-${port}"

cd "${repo_root}"

exec bazel-bin/src/bridge/parent_ws_bridge \
  --interface "${MOE_BRIDGE_INTERFACE}" \
  --port "${port}" \
  --token "${MOE_BRIDGE_TOKEN}" \
  --parent bazel-bin/src/parent/workspace_parent \
  --cwd "${repo_root}" \
  --state-directory "${state_directory}"
