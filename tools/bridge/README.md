# Browser Bridge Service

The bridge can run as a systemd user service. The service expects `bazel` on the
user service PATH.

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
ln -sf "$PWD/tools/bridge/my-opiniated-editor-bridge@.service" \
  ~/.config/systemd/user/my-opiniated-editor-bridge@.service
systemctl --user daemon-reload
systemctl --user enable --now my-opiniated-editor-bridge@7682.service
```

The systemd instance name is the bridge port. Start a separate manual-testing
instance without interrupting port 7682:

```bash
systemctl --user enable --now my-opiniated-editor-bridge@7683.service
```

Each port owns its protobuf registry at
`$XDG_STATE_HOME/my-opiniated-editor/instances/port-<port>/worktrees.pb`, or
`$HOME/.local/state/my-opiniated-editor/instances/port-<port>/worktrees.pb`
when `XDG_STATE_HOME` is unset.

Start it at boot, not only after login:

```bash
loginctl enable-linger "$USER"
```

Restart manually:

```bash
tools/bridge/restart_bridge.sh 7683
systemctl --user status my-opiniated-editor-bridge@7683.service
```

Connect from a browser:

```text
http://<server-ip>:7682/?token=<MOE_BRIDGE_TOKEN>
```
