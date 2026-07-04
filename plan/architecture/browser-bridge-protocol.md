# Browser Bridge Protocol

## Transport

Use HTTPS plus WebSocket.

Initial endpoints:

- `GET /`: browser app assets.
- `GET /health`: bridge health.
- `WS /ws/control`: structured control and workspace events.
- `WS /ws/pty/main`: raw terminal bytes for the parent workspace PTY.

Do not expose a WebSocket per shell or agent pane in the first design. The
bridge serves one parent C++ workspace app. Shells and agents are child PTYs
inside that parent app, and the parent app decides how to tile, focus, resize,
log, and supervise them.

## Control Messages

Use JSON for the sideband protocol. Keep every message typed.

The control socket is a sideband. It is not the main command plane for the
workspace UI. Most user commands are handled by the parent C++ app through its
own keymap over the PTY. The sideband exists for browser-specific capabilities
such as clipboard writes, permission/status reporting, reconnect metadata, and
future optional browser-native inspector views.

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

Client capabilities:

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

Request/response:

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

## Clipboard Protocol

The Linux server cannot directly write the browser client's clipboard. Clipboard
write is always a server request and browser action.

Server to browser:

```json
{
  "type": "clipboard.write.request",
  "id": "clip_456",
  "mime": "text/plain",
  "text": "content to copy",
  "reason": "copy.current_symbol"
}
```

Browser to server:

```json
{
  "type": "clipboard.write.result",
  "id": "clip_456",
  "ok": true
}
```

Also support OSC 52 from the parent PTY stream:

```text
ESC ] 52 ; c ; <base64 text> BEL
```

The bridge should detect OSC 52 in parent PTY output, suppress or pass it
according to policy, and send a `clipboard.write.request` to the browser.

The parent app can also send explicit clipboard requests over the control
sideband. Prefer the sideband for rich commands and keep OSC 52 for terminal
compatibility.

## Authentication

For localhost-only development, a random session token is enough.

For remote access:

- Require HTTPS.
- Require an authenticated browser session.
- Bind WebSocket sessions to the authenticated HTTP session.
- Add CSRF protection to state-changing HTTP endpoints.
- Keep clipboard writes auditable because clipboard is a high-trust channel.

## Compatibility Policy

The parent PTY stream is raw terminal bytes and has no application protocol
version.

The JSON sideband uses handshake-level versioning only. The likely failure mode
is an old browser tab talking to a newer bridge after a restart; the right MVP
behavior is to fail clearly and reload the browser client.
