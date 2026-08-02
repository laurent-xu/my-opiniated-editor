#!/usr/bin/env bash
set -euo pipefail

# Run the already-built parent PTY browser bridge on a loopback-only port.
# The matching HTTPS proxy owns the public port. Keeping the state directory
# keyed by the public port preserves instance identity across this split.

if [[ "$#" -lt 2 || "$#" -gt 3 || ! "$1" =~ ^[0-9]{1,5}$ ||
  ! "$2" =~ ^[0-9]{1,5}$ ]]; then
  echo "usage: $0 <http-port> <https-port> [worktree]" >&2
  exit 2
fi
http_port="$((10#$1))"
https_port="$((10#$2))"
if ((http_port < 1 || http_port > 65535 || https_port < 1 || https_port > 65535)); then
  echo "ports must be between 1 and 65535" >&2
  exit 2
fi
if ((http_port == https_port)); then
  echo "HTTP and HTTPS ports must differ" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
worktree_argument="${3:-${MOE_BRIDGE_WORKTREE:-${repo_root}}}"
if [[ ! -d "${worktree_argument}" ]]; then
  echo "worktree must be an existing directory: ${worktree_argument}" >&2
  exit 2
fi
worktree="$(cd -- "${worktree_argument}" && pwd -P)"

if [[ -n "${XDG_STATE_HOME:-}" ]]; then
  state_root="${XDG_STATE_HOME}"
else
  : "${HOME:?HOME is required when XDG_STATE_HOME is unset}"
  state_root="${HOME}/.local/state"
fi
state_directory="${state_root}/my-opiniated-editor/instances/port-${https_port}"

cd "${worktree}"

exec bazel-bin/src/bridge/parent_ws_bridge \
  --interface 127.0.0.1 \
  --port "${http_port}" \
  --parent bazel-bin/src/parent/workspace_parent \
  --cwd "${worktree}" \
  --state-directory "${state_directory}"
