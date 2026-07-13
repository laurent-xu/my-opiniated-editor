#!/usr/bin/env bash
set -euo pipefail

# Run the parent PTY browser bridge.
# systemd uses this script as ExecStart, and it also works directly from a shell.

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
home_dir="${HOME:-}"

# Load private local configuration if present. MOE_BRIDGE_ENV wins; otherwise use
# the user config path, then the repo-local gitignored fallback.
default_env=""
if [[ -n "${home_dir}" ]]; then
  default_env="${home_dir}/.config/my-opiniated-editor/bridge.env"
fi
repo_env="${repo_root}/tools/bridge/bridge.env"
if [[ -n "${MOE_BRIDGE_ENV:-}" ]]; then
  if [[ ! -f "${MOE_BRIDGE_ENV}" ]]; then
    printf 'run_bridge.sh: MOE_BRIDGE_ENV file does not exist: %s\n' "${MOE_BRIDGE_ENV}" >&2
    exit 1
  fi
  # shellcheck disable=SC1090
  source "${MOE_BRIDGE_ENV}"
elif [[ -n "${default_env}" && -f "${default_env}" ]]; then
  # shellcheck disable=SC1090
  source "${default_env}"
elif [[ -f "${repo_env}" ]]; then
  # shellcheck disable=SC1090
  source "${repo_env}"
fi

MOE_BRIDGE_INTERFACE="${MOE_BRIDGE_INTERFACE:-127.0.0.1}"
MOE_BRIDGE_PORT="${MOE_BRIDGE_PORT:-7682}"
MOE_BRIDGE_WORKSPACE="${MOE_BRIDGE_WORKSPACE:-${repo_root}}"
MOE_BRIDGE_BUILD="${MOE_BRIDGE_BUILD:-1}"
MOE_BRIDGE_BRIDGE_BINARY="${MOE_BRIDGE_BRIDGE_BINARY:-${repo_root}/bazel-bin/src/bridge/parent_ws_bridge}"
MOE_BRIDGE_PARENT_BINARY="${MOE_BRIDGE_PARENT_BINARY:-${repo_root}/bazel-bin/src/parent/workspace_parent}"

find_bazel() {
  if [[ -n "${MOE_BRIDGE_BAZEL:-}" ]]; then
    printf '%s\n' "${MOE_BRIDGE_BAZEL}"
    return
  fi
  if command -v bazel >/dev/null 2>&1; then
    command -v bazel
    return
  fi
  if command -v bazelisk >/dev/null 2>&1; then
    command -v bazelisk
    return
  fi
  if [[ -x /run/current-system/sw/bin/bazel ]]; then
    printf '%s\n' /run/current-system/sw/bin/bazel
    return
  fi
  if [[ -x /run/current-system/sw/bin/bazelisk ]]; then
    printf '%s\n' /run/current-system/sw/bin/bazelisk
    return
  fi
  printf 'run_bridge.sh: cannot find bazel or bazelisk\n' >&2
  return 1
}

is_network_bind() {
  [[ "${MOE_BRIDGE_INTERFACE}" != "127.0.0.1" && "${MOE_BRIDGE_INTERFACE}" != 127.* ]]
}

if is_network_bind && [[ -z "${MOE_BRIDGE_TOKEN:-}" ]]; then
  printf 'run_bridge.sh: MOE_BRIDGE_TOKEN is required when binding %s\n' \
    "${MOE_BRIDGE_INTERFACE}" >&2
  exit 1
fi

cd "${repo_root}"

# Build before starting so the service follows the current checkout. Set
# MOE_BRIDGE_BUILD=0 when running against prebuilt binaries.
if [[ "${MOE_BRIDGE_BUILD}" != "0" ]]; then
  bazel_binary="$(find_bazel)"
  "${bazel_binary}" --batch build \
    //src/bridge:parent_ws_bridge \
    //src/parent:workspace_parent
fi

args=(
  --interface "${MOE_BRIDGE_INTERFACE}"
  --port "${MOE_BRIDGE_PORT}"
  --parent "${MOE_BRIDGE_PARENT_BINARY}"
  --cwd "${MOE_BRIDGE_WORKSPACE}"
)

if [[ -n "${MOE_BRIDGE_TOKEN:-}" ]]; then
  args+=(--token "${MOE_BRIDGE_TOKEN}")
elif [[ "${MOE_BRIDGE_ALLOW_UNAUTHENTICATED_NETWORK:-0}" == "1" ]]; then
  args+=(--allow-unauthenticated-network)
fi

exec "${MOE_BRIDGE_BRIDGE_BINARY}" "${args[@]}"
