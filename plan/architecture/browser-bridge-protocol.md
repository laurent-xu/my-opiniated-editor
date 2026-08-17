# Browser Bridge Protocol

## Transport

Use HTTPS plus WebSocket.

Current Phase 1 bridge:

- `//src/bridge:parent_pty_session_test` covers the owned C++ `forkpty`
  foundation. The bridge keeps one `ParentPtySession` alive across browser
  disconnect/reconnect and attaches clients to that session.
- `//src/bridge:parent_ws_bridge_integration_test` proves the first owned
  bridge server does that over WebSocket.
- The bridge now treats applications and WebSocket connections as separate
  axes. The current implementation has one parent PTY application served to
  multiple simultaneous WebSocket clients. The broader architecture should allow
  an N-to-N mapping: any application session can be served by any authorized
  browser socket, and one browser socket can choose which application/session it
  is observing or controlling.

Current owned bridge endpoints:

- `GET /`: thin browser client.
- `GET /client.js`: xterm.js/WebSocket client code.
- `GET /style.css`: minimal terminal layout.
- `GET /health`: bridge health and current parent PID.
- `WS /ws`: parent PTY bytes. Client input frames use command byte `0`; server
  output frames use command byte `0`; resize frames use command byte `1` plus
  JSON `{columns, rows}`. Anonymous tray switching uses command byte `2` plus
  JSON `{tray}`. Toggling the active tray's parent-owned worktree manager uses
  command byte `3` with an empty payload. The manager contains worktree
  switching, worktree creation, and repository registration. Toggling
  parent-owned command mode uses command byte `5` with an empty payload.
  Worktree confirmation actions use command byte `6`, overlay navigation uses
  command byte `7`, and typed pane actions use command byte `8`. Pane actions
  name intent such as `splitLeftToRight`, `toggleSelectionOrSwap`,
  `toggleMove`, `confirmMove`, and `rotate`; the bridge does not interpret or
  own the resulting layout.
- Browser pane viewport reports use command byte `9` followed by the tray key,
  pane ID, rows, and columns in a compact binary payload. The bridge frames the
  report onto the parent view socket. The parent accepts reports for an active
  pane view or a visible inactive-tray preview and ignores unknown tray or pane
  identities.
- Server status frames use command byte `1` plus a `parent.status` JSON object.
  The parent sends these events to the bridge over a dedicated inherited pipe,
  separate from terminal output. The bridge caches the latest status and sends
  it to every connected or newly attached WebSocket.
  Status includes the parent-owned pane interaction mode (`none`, `selection`,
  `moveTarget`, `moveDrop`, or `swapTarget`) and the selected source-node
  count, allowing contextual browser hints and reconnect without browser-owned
  pane state.
- Server pane output frames use command byte `2`. Their payload identifies the
  parent tray and pane, followed by the child PTY bytes unchanged. The bridge
  keeps message boundaries and a bounded reconnect backlog but does not
  interpret pane topology.
- Parent status includes the active normalized N-ary `paneView`, focused pane,
  maximize state, selection, and move highlighting. The browser treats this as
  a view description and retains one xterm.js instance for each pane ID.
- While the worktree picker highlights a live tray, parent status also includes
  a read-only `panePreview` with terminal-grid geometry and that tray's pane
  view. The browser mounts the same pane DOM primitives and xterm.js instances
  used by the normal tray, so separators stay one CSS pixel and raw child PTY
  output continues incrementally beneath the picker. Preview fitting preserves
  each pane's normal column count and does not resize its PTY, avoiding xterm
  scrollback reflow; only the visible preview row count changes.
- The outer browser terminal remains transparent while worktree management is
  open. Parent status identifies the first row owned by the current picker or
  form, and the browser places one opaque DOM backing rectangle under only that
  region. Live active-tray panes remain visible elsewhere, while a dedicated
  pane preview can be mounted above the overlay. The parent renders full ANSI
  tray snapshots only when no browser pane view channel exists, preserving the
  direct parent-PTY fallback without exposing its cell-wide separators in the
  browser.
- The browser connects directly to `/ws`; bridge URLs do not carry
  authentication credentials.
- Multiple clients can connect to `/ws` at the same time. A single PTY reader
  broadcasts parent output to all clients; each client can send input or resize
  frames back to the parent PTY. New clients receive the recent terminal
  backlog so refresh/reconnect does not start from a blank terminal surface.
  Command mode, active tray identity, active overlay identity, and pane
  interaction state come from the parent status event rather than per-browser
  guesses.

Current service transport:

- Browser traffic is served by a standalone Python HTTPS proxy; the C++ bridge
  listens over plain HTTP on an explicitly configured loopback-only port.
- Tailscale Funnel public port `443` publishes the loopback-only `7682` proxy,
  which forwards to `127.0.0.1:17682`. Public port `10000` similarly publishes
  the loopback-only `7683` proxy, which forwards to `127.0.0.1:17683`. The two
  bridge instances retain separate parent and workspace state.
- The proxy requires HTTP Basic Auth for every HTTP request and WebSocket
  upgrade. Browsers retain successful credentials for the origin, so reloads
  and reconnects do not prompt repeatedly.
- A configured `MOE_BRIDGE_ALLOWED_ORIGIN` makes the proxy reject WebSocket
  upgrades with a missing `Origin` header or one outside its comma-separated
  exact HTTPS allowlist before checking credentials. Funnel deployments allow
  their exact HTTPS `*.ts.net` origin and may explicitly allow a loopback
  browser origin for local testing.
- The password file under `~/.secrets` stores only the username, scrypt
  parameters, random salt, and digest. TLS certificate and key files are also
  kept outside the repository.

Initial endpoints:

- `GET /`: browser app assets.
- `GET /health`: bridge health.
- `WS /ws/control`: structured control and workspace events.
- `WS /ws/pty/main`: raw terminal bytes for the parent workspace PTY.

The current owned bridge uses `/ws` as a temporary single-socket endpoint. Split
to `/ws/pty/main` plus `/ws/control` when browser-specific messages grow beyond
resize and status reporting.

Do not expose bridge-owned shell or agent pane topology in the first design.
The bridge serves application sessions; today that is one parent C++ workspace
app. Shells and agents are child PTYs inside that parent app, and the parent app
decides how to tile, focus, resize, log, and supervise them. Later, the bridge
can route browser sockets across multiple parent-owned application/session
surfaces without making the browser own those processes.

## Control Messages

Use JSON for the sideband protocol. Keep every message typed.

The control socket is a sideband. It is not the main command plane for the
workspace UI. Most user commands are handled by the parent C++ app through its
own keymap over the PTY. The sideband exists for browser-specific capabilities
such as permission/status reporting, reconnect metadata, future clipboard over
HTTPS, and future optional browser-native inspector views.

Version the sideband once at connection time:

```json
{
  "type": "hello",
  "protocol": "workspace-control",
  "version": 1,
  "client": "browser"
}
```

If the client and server sideband versions do not match, close the socket with
a clear reload-required error. Do not add a version field to every message for
the MVP.

Future client capabilities:

```json
{
  "type": "client.capabilities",
  "clipboardWrite": true,
  "platform": "macos",
  "userAgent": "browser"
}
```

Browser status event:

```json
{
  "type": "browser.status",
  "clipboard": {
    "writePermission": "granted"
  }
}
```

Server event:

```json
{
  "type": "event",
  "topic": "parent.status",
  "workspaceId": "ws_main",
  "payload": {
    "title": "my-opiniated-editor",
    "state": "ready"
  }
}
```

Future request/response:

```json
{
  "type": "reply",
  "replyTo": "clip_456",
  "ok": true,
  "result": {
    "clipboard": "written"
  }
}
```

## Parent PTY Messages

For `WS /ws/pty/main`:

- Binary client frame: bytes to write to the parent app's PTY stdin.
- Binary server frame: bytes read from the parent app's PTY stdout/stderr
  stream.
- Text client frame: small JSON controls for the parent PTY, such as resize.

Resize:

```json
{
  "type": "resize",
  "cols": 120,
  "rows": 40
}
```

## Clipboard

Status: parked. Phase 1 does not expose a parent copy command, terminal
clipboard broker, or browser Clipboard API call.

Revisit clipboard only after HTTPS or another secure browser context is part of
the bridge path. When that happens, design the smallest protocol needed for the
actual workflow rather than carrying a speculative clipboard protocol now.

## Authentication

Current implementation:

- The C++ bridge is unauthenticated plumbing and accepts only loopback
  interfaces.
- Non-loopback binds, including `0.0.0.0`, are rejected without an override.
- Browser and WebSocket URLs do not carry authentication credentials.
- The HTTPS proxy can require an exact HTTPS `Origin` value on every WebSocket
  upgrade; the Funnel instance enables this check.

For remote access:

- Require HTTPS.
- Require an authenticated browser session.
- Bind WebSocket sessions to the authenticated HTTP session.
- Add CSRF protection to state-changing HTTP endpoints.
- Keep future clipboard writes auditable because clipboard is a high-trust
  channel.

The current Funnel service paths satisfy the first three requirements with the
HTTPS/auth proxy. Service deployments keep the authenticated proxies and
unauthenticated C++ bridges on loopback-only origin ports.

## Compatibility Policy

The parent PTY stream is raw terminal bytes and has no application protocol
version.

The JSON sideband uses handshake-level versioning only. The likely failure mode
is an old browser tab talking to a newer bridge after a restart; the right MVP
behavior is to fail clearly and reload the browser client.
