#!/usr/bin/env bash
set -euo pipefail

# Build first, then restart one port-keyed systemd user service.
if [[ "$#" -lt 1 || "$#" -gt 2 || ! "$1" =~ ^[0-9]{1,5}$ ]]; then
  echo "usage: $0 <port> [worktree]" >&2
  exit 2
fi
port="$((10#$1))"
if ((port < 1 || port > 65535)); then
  echo "HTTPS port must be between 1 and 65535" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
worktree_argument="${2:-${repo_root}}"
if [[ ! -d "${worktree_argument}" ]]; then
  echo "worktree must be an existing directory: ${worktree_argument}" >&2
  exit 2
fi
worktree="$(cd -- "${worktree_argument}" && pwd -P)"

cd "${worktree}"

bazel --batch build \
  //src/bridge:parent_ws_bridge \
  //src/parent:workspace_parent

bridge_service="my-opiniated-editor-bridge@${port}.service"
https_service="my-opiniated-editor-bridge-https@${port}.service"
systemd_worktree="${worktree//\\/\\\\}"
systemd_worktree="${systemd_worktree//\"/\\\"}"
printf '[Service]\nEnvironment="MOE_BRIDGE_WORKTREE=%s"\n' "${systemd_worktree}" |
  systemctl --user edit --runtime --stdin --drop-in=50-worktree.conf \
    "${bridge_service}"
systemctl --user restart "${bridge_service}"
systemctl --user restart "${https_service}"
