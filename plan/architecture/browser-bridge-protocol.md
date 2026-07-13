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
  JSON `{columns, rows}`.
- When `--token` is configured, `/`, `/health`, and `/ws` require the token.
  The browser client carries it from `/?token=<token>` to `/ws?token=<token>`.
  Static `/client.js` and `/style.css` are not sensitive and remain fetchable
  without the token.
- Multiple clients can connect to `/ws` at the same time. A single PTY reader
  broadcasts parent output to all clients; each client can send input or resize
  frames back to the parent PTY. New clients receive the recent terminal
  backlog so refresh/reconnect does not start from a blank terminal surface.

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

For localhost-only development, a random session token is enough.

Current implementation:

- Loopback binds may run without a token for local development.
- Non-loopback binds, including `0.0.0.0`, require `--token <secret>` unless
  `--allow-unauthenticated-network` is passed.
- The token is a development guard, not the final remote-access security model.

For remote access:

- Require HTTPS.
- Require an authenticated browser session.
- Bind WebSocket sessions to the authenticated HTTP session.
- Add CSRF protection to state-changing HTTP endpoints.
- Keep future clipboard writes auditable because clipboard is a high-trust
  channel.

## Compatibility Policy

The parent PTY stream is raw terminal bytes and has no application protocol
version.

The JSON sideband uses handshake-level versioning only. The likely failure mode
is an old browser tab talking to a newer bridge after a restart; the right MVP
behavior is to fail clearly and reload the browser client.
