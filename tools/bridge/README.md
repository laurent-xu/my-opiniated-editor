# Browser Bridge Services

The bridge path is split deliberately:

```text
LAN browser -> HTTPS/auth proxy :7682 -> HTTP C++ bridge 127.0.0.1:17682
Tailscale Funnel -> HTTPS/auth proxy 127.0.0.1:7683
                 -> HTTP C++ bridge 127.0.0.1:17683
```

The small Python proxy terminates TLS, verifies HTTP Basic credentials, and
forwards HTTP and WebSocket traffic. The C++ bridge and parent PTY remain the
application. Both layers bind loopback by default. The `7682` instance example
explicitly opts into `0.0.0.0` for LAN access; the `7683` example remains on
loopback for Tailscale Funnel. Neither plain-HTTP upstream is reachable
off-host.

When `MOE_BRIDGE_ALLOWED_ORIGIN` is set for an instance, the proxy rejects a
WebSocket upgrade whose `Origin` header is missing or differs from that exact
HTTPS origin. Use the browser origin without a trailing slash.

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
again on every HTTP request, reload, or WebSocket reconnect. After a failed
credential check, the proxy waits three seconds before accepting another
authentication attempt from any client. The proxy enforces this cooldown on
the server with a monotonic clock.

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
run_bridge.sh <http-port> <https-port> [worktree]
run_https_proxy.sh <http-port> <https-port> [worktree]
```

Enable linger if the services must start before interactive login:

```bash
loginctl enable-linger "$USER"
```

NixOS must allow the LAN port. Funnel does not require opening `7683`:

```nix
networking.firewall.allowedTCPPorts = [ 7682 ];
```

Connect from any allowed LAN host to the LAN instance:

```text
https://<server-ip>:7682/
```

Use `notmyfoo` and the configured password. Each public port owns separate
parent state under
`$XDG_STATE_HOME/my-opiniated-editor/instances/port-<public-port>`, or
`$HOME/.local/state/my-opiniated-editor/instances/port-<public-port>` when
`XDG_STATE_HOME` is unset.

## Expose Port 7683 Through Tailscale Funnel

Enable Tailscale on NixOS and join the machine to the intended tailnet:

```nix
services.tailscale.enable = true;
```

```bash
sudo nixos-rebuild switch
sudo tailscale up
```

Find the machine's full `*.ts.net` DNS name in `tailscale status --json` or in
the Tailscale admin console. Put its exact HTTPS origin in the private instance
file, with no trailing slash:

```text
MOE_BRIDGE_HTTP_PORT=17683
MOE_BRIDGE_HTTPS_INTERFACE=127.0.0.1
MOE_BRIDGE_ALLOWED_ORIGIN=https://nixos.example-tailnet.ts.net
```

Restart the `7683` bridge pair, then publish only its authenticated HTTPS
proxy:

```bash
tools/bridge/restart_bridge.sh 7683
sudo tailscale funnel --bg https+insecure://127.0.0.1:7683
sudo tailscale funnel status
```

`https+insecure` applies only to Funnel's loopback connection to the proxy's
self-signed certificate. The public Funnel endpoint still uses a
browser-trusted Tailscale certificate. Never point Funnel at the unauthenticated
HTTP bridge on `17683`.

## Rebuild And Restart

Restart one complete HTTP/HTTPS pair after rebuilding:

```bash
tools/bridge/restart_bridge.sh 7682
tools/bridge/restart_bridge.sh 7683
tools/bridge/restart_bridge.sh 7683 /path/to/worktree
```

The optional worktree path selects the checkout whose Python HTTPS proxy,
prebuilt bridge, and parent binaries run for that public port.
`restart_bridge.sh` builds that checkout and keeps the selection on both
service instances until reboot or the next restart helper invocation. Its
HTTPS service override directly selects the worktree launcher, so deployment
also works while the installed service template still predates that launcher.
Without the argument, each script uses the checkout that contains the invoked
script.

Restarting the C++ bridge also restarts that instance's parent PTY. Browser
reload and WebSocket reconnect do not.
