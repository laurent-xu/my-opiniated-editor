#!/usr/bin/env bash
set -euo pipefail

# Restart the systemd user service that daemonizes the browser bridge.
systemctl --user restart my-opiniated-editor-bridge.service
