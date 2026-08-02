#!/usr/bin/env bash
set -euo pipefail

# Run the HTTPS/auth proxy from the same selected worktree as the C++ bridge.
# The systemd template invokes this stable launcher from the main checkout;
# MOE_BRIDGE_WORKTREE redirects the implementation for feature deployments.

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

cd "${worktree}"

exec tools/bridge/https_proxy.py serve \
  --http-port "${http_port}" \
  --https-port "${https_port}"
