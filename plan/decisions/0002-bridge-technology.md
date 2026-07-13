# Decision 0002: Bridge Technology

Status: Updated

## Decision

Build the persistent browser bridge path in C++ around the owned parent PTY
session.

The bridge must still expose only one primary parent PTY. Shells and agents
remain child PTYs owned by the parent C++ workspace app.

## Context

The bridge is plumbing, not the product. It must:

- Serve or integrate with the browser terminal surface.
- Authenticate browser sessions or sit behind a reverse proxy that does.
- Spawn/connect the one parent workspace PTY.
- Forward input/output bytes.
- Forward resize.
- Reconnect cleanly.

Clipboard is intentionally outside the current bridge scope. Revisit it after
HTTPS/reverse-proxy support exists.

The parent C++ app owns process topology and product behavior.

## Current Evidence

As of 2026-07-07:

- `//src/bridge:parent_pty_session_test` covers the C++ `forkpty` foundation
  needed for an owned persistent bridge.
- `//src/bridge:parent_ws_bridge_integration_test` covers the first owned C++
  WebSocket bridge: health, terminal I/O, multiple browser clients, and
  reconnect to the same parent PID.
- The owned bridge now separates parent application lifetime from browser
  socket lifetime: one parent PTY application can be served by multiple
  simultaneous WebSockets. Future routing can generalize this to N application
  sessions and N browser sockets without making browser sockets own process
  topology.
- Clipboard support was intentionally removed from the Phase 1 implementation
  and parked until HTTPS/reverse-proxy support exists.
- The owned bridge now serves a thin xterm.js browser client that connects to
  `/ws`.
- Non-loopback owned bridge binds require a development token unless an
  explicit unsafe override is passed.

The implementation path is the owned C++ bridge.

## Fit Criteria

An external bridge is acceptable only if it can support:

- One parent process/PTY.
- Correct resize and reconnect behavior.
- Full-screen terminal programs.
- Future HTTPS clipboard path.
- Localhost/private-network deployment.
- Enough customization for auth/status/sideband needs without forking too much.

If those require invasive changes, building a small C++ bridge is cleaner.

## Consequences

Benefits:

- Avoids handing parent process lifetime to the browser bridge.
- Keeps the productivity-critical parent process model in C++.

Costs:

- We now own WebSocket/session/reconnect plumbing earlier.
- Future HTTPS clipboard behavior still needs design and verification.
