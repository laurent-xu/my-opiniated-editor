#!/usr/bin/env bash
set -euo pipefail

# Restart the systemd user service that daemonizes the browser bridge.
SERVICE_NAME="${MOE_BRIDGE_SERVICE_NAME:-my-opiniated-editor-bridge.service}"

systemctl --user restart "${SERVICE_NAME}"
systemctl --user --no-pager --lines=20 status "${SERVICE_NAME}"
