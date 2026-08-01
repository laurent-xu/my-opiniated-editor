#!/usr/bin/env bash
set -euo pipefail

# Build first, then restart one port-keyed systemd user service.
if [[ "$#" -ne 1 || ! "$1" =~ ^[0-9]{1,5}$ ]]; then
  echo "usage: $0 <port>" >&2
  exit 2
fi
port="$((10#$1))"
if ((port < 1 || port > 65535)); then
  echo "HTTPS port must be between 1 and 65535" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

cd "${repo_root}"

bazel --batch build \
  //src/bridge:parent_ws_bridge \
  //src/parent:workspace_parent

systemctl --user daemon-reload
systemctl --user restart "my-opiniated-editor-bridge@${port}.service"
systemctl --user restart "my-opiniated-editor-bridge-https@${port}.service"
