# Decision 0002: Bridge Technology

Status: Proposed

## Decision

Prefer a proven open-source bridge if it fits the parent-PTY architecture. If
the bridge must be built or substantially owned, build it in C++.

Evaluation order:

1. Evaluate `ttyd` first.
2. Evaluate WeTTY if `ttyd` does not fit the browser/control-overlay path.
3. Do not choose GoTTY solely for star count because it appears much less
   maintained.
4. If no existing bridge fits, build a small C++ bridge.

The bridge must still expose only one primary parent PTY. Shells and agents
remain child PTYs owned by the parent C++ workspace app.

## Context

The bridge is plumbing, not the product. It must:

- Serve or integrate with the browser terminal surface.
- Authenticate browser sessions or sit behind a reverse proxy that does.
- Spawn/connect the one parent workspace PTY.
- Forward input/output bytes.
- Forward resize.
- Support browser clipboard requests through OSC 52 and/or a small sideband.
- Reconnect cleanly.

The parent C++ app owns process topology and product behavior.

## Current Evidence

As of 2026-07-04:

- `ttyd` is a purpose-built terminal-over-web tool, native/C-based, with about
  12k GitHub stars and broad packaging/use.
- WeTTY is Node-based, uses xterm.js, has about 5.3k GitHub stars, and shows a
  2026 release.
- GoTTY has more stars, but its latest GitHub release shown is from 2017.

This points to `ttyd` as the first bridge candidate, WeTTY as a second
candidate, and GoTTY as less attractive despite its larger historical audience.

## Fit Criteria

An external bridge is acceptable only if it can support:

- One parent process/PTY.
- Correct resize and reconnect behavior.
- Full-screen terminal programs.
- Browser clipboard path.
- Localhost/private-network deployment.
- Enough customization for auth/status/sideband needs without forking too much.

If those require invasive changes, building a small C++ bridge is cleaner.

## Consequences

Benefits:

- Avoids owning bridge plumbing too early.
- Uses existing terminal-over-web hardening where possible.
- Keeps C++ ownership if the bridge becomes custom.

Costs:

- Need a short bridge evaluation spike.
- External bridges may constrain clipboard/status sideband behavior.
- If we switch from external bridge to C++, early throwaway work may be lost.

