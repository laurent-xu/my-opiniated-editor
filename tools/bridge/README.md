# Browser Bridge Service

The bridge can run as a systemd user service. On NixOS, make `bazelisk` or
`bazel` available outside `nix shell`, or set `MOE_BRIDGE_BAZEL` in the env file.

Create private config:

```bash
mkdir -p ~/.config/my-opiniated-editor
cp tools/bridge/bridge.env.example ~/.config/my-opiniated-editor/bridge.env
chmod 600 ~/.config/my-opiniated-editor/bridge.env
$EDITOR ~/.config/my-opiniated-editor/bridge.env
```

Install and start the user service:

```bash
mkdir -p ~/.config/systemd/user
ln -sf "$PWD/tools/bridge/my-opiniated-editor-bridge.service" \
  ~/.config/systemd/user/my-opiniated-editor-bridge.service
systemctl --user daemon-reload
systemctl --user enable --now my-opiniated-editor-bridge.service
```

Start it at boot, not only after login:

```bash
loginctl enable-linger "$USER"
```

Restart manually:

```bash
tools/bridge/restart_bridge.sh
```

Connect from a browser:

```text
http://<server-ip>:7682/?token=<MOE_BRIDGE_TOKEN>
```
