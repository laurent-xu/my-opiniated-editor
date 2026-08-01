# Browser Bridge Services

The public bridge path is split deliberately:

```text
browser -> HTTPS/auth proxy :7682 -> HTTP C++ bridge 127.0.0.1:17682
browser -> HTTPS/auth proxy :7683 -> HTTP C++ bridge 127.0.0.1:17683
```

The small Python proxy terminates TLS, verifies HTTP Basic credentials, and
forwards HTTP and WebSocket traffic. The C++ bridge and parent PTY remain the
application. Both public proxy instances bind `0.0.0.0` by default so other LAN
hosts can connect; neither plain-HTTP upstream is reachable off-host.

## One-Time Secret Setup

Create the scrypt password hash and self-signed LAN certificate. This prompts
for the password twice during setup, never places plaintext in argv, and writes
private files under `~/.secrets` with mode `0600`:

```bash
nix shell nixpkgs#openssl --command \
  tools/bridge/https_proxy.py create-secrets --username notmyfoo
```

The files are:

- `~/.secrets/my-opiniated-editor-password`: username, scrypt parameters,
  random salt, and password digest.
- `~/.secrets/my-opiniated-editor-certificate.pem`: self-signed certificate
  with the current host names and LAN IPv4 addresses.
- `~/.secrets/my-opiniated-editor-certificate-key.pem`: TLS private key.

The browser will require a one-time exception for the self-signed certificate.
It caches successful Basic Auth credentials for the origin, so it does not ask
again on every HTTP request, reload, or WebSocket reconnect.

## Install Both LAN Ports

The common optional environment file controls the public bind interface and
custom secret paths. Per-instance files make both the HTTP and HTTPS ports
explicit:

```bash
mkdir -p ~/.config/my-opiniated-editor
cp tools/bridge/bridge.env.example \
  ~/.config/my-opiniated-editor/bridge.env
cp tools/bridge/bridge-7682.env.example \
  ~/.config/my-opiniated-editor/bridge-7682.env
cp tools/bridge/bridge-7683.env.example \
  ~/.config/my-opiniated-editor/bridge-7683.env
chmod 600 ~/.config/my-opiniated-editor/bridge*.env
```

Install the two user service templates:

```bash
mkdir -p ~/.config/systemd/user
ln -sf "$PWD/tools/bridge/my-opiniated-editor-bridge@.service" \
  ~/.config/systemd/user/my-opiniated-editor-bridge@.service
ln -sf "$PWD/tools/bridge/my-opiniated-editor-bridge-https@.service" \
  ~/.config/systemd/user/my-opiniated-editor-bridge-https@.service
systemctl --user daemon-reload
systemctl --user enable --now \
  my-opiniated-editor-bridge-https@7682.service \
  my-opiniated-editor-bridge-https@7683.service
```

The HTTPS services require and start their matching HTTP bridge services.
Their commands receive both ports explicitly; there is no arithmetic port
mapping in either launcher:

```text
run_bridge.sh <http-port> <https-port>
https_proxy.py serve --http-port <http-port> --https-port <https-port>
```

Enable linger if the services must start before interactive login:

```bash
loginctl enable-linger "$USER"
```

NixOS must allow both public ports:

```nix
networking.firewall.allowedTCPPorts = [ 7682 7683 ];
```

Connect from any allowed LAN host:

```text
https://<server-ip>:7682/
https://<server-ip>:7683/
```

Use `notmyfoo` and the configured password. Each public port owns separate
parent state under
`$XDG_STATE_HOME/my-opiniated-editor/instances/port-<public-port>`, or
`$HOME/.local/state/my-opiniated-editor/instances/port-<public-port>` when
`XDG_STATE_HOME` is unset.

## Rebuild And Restart

Restart one complete HTTP/HTTPS pair after rebuilding:

```bash
tools/bridge/restart_bridge.sh 7682
tools/bridge/restart_bridge.sh 7683
```

Restarting the C++ bridge also restarts that instance's parent PTY. Browser
reload and WebSocket reconnect do not.
