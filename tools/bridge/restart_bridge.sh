#!/usr/bin/env bash
set -euo pipefail

# Build first, then restart the systemd user service.
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

cd "${repo_root}"

bazel --batch build \
  //src/bridge:parent_ws_bridge \
  //src/parent:workspace_parent

systemctl --user restart my-opiniated-editor-bridge.service
