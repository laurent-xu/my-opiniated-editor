# Decision 0001: Own The Parent C++ Workspace Process

Status: Proposed

## Decision

Build an owned C++ parent workspace/editor process and serve it through one
primary browser PTY.

The browser bridge should not expose separate PTY sessions for every shell,
agent, or pane in the first architecture. It should attach the browser to the
parent process. The parent C++ app owns tiling, editor behavior, command
routing, fuzzy finding, agent sessions, child PTYs, LSP/build state, diagnostics,
plan review, plan diffing, git workspace review, and feedback routing.

Use browser-native UI only for the thin host layer at first: xterm.js terminal
surface, auth/session handling, reconnect status, and maybe a small control
overlay. Clipboard can be revisited after HTTPS support exists. CodeMirror 6 can
be revisited later as an optional view adapter, not as the first editor
substrate.

## Context

The workspace needs to combine:

- Shells.
- Coding-agent CLIs.
- Text editing.
- Tiling.
- Fuzzy navigation.
- LSP diagnostics.
- Bazel targets.
- Project environments.
- Agent plan review.
- Agent plan proposal diffing.
- Section-level feedback.
- Git workspace diff and latest-updated-file review.
- Future clipboard control from server commands to browser client, after HTTPS
  support exists.

The desired ownership boundary is C++-first. A single parent PTY keeps the
bridge simple and lets the C++ app be the true workspace. Shell and agent CLIs
can still be PTY-backed, but those PTYs are implementation details inside the
parent app.

Plan review and diagnostics still need stable structured objects. The important
point is that these objects should live in the parent app's model, not only in
rendered terminal text. The parent app can render them in the terminal UI and
also expose selected structured data over a sideband later.

## Consequences

Benefits:

- Strongest ownership of the productivity surface in C++.
- Simple browser bridge: one parent PTY plus a small control sideband.
- Parent-owned tiling, fuzzy navigation, editor behavior, shells, and agents.
- A future browser clipboard path can be added without changing parent process
  ownership.
- No bridge-owned per-pane PTY/session topology.
- Straight path to a terminal-native workflow that works from any browser.

Costs:

- The C++ terminal UI must become genuinely good.
- Rich browser-native inspector views are deferred.
- The parent app must store structured plans/diagnostics explicitly so the
  system does not depend on scraping rendered screen text.
- Mobile review may be less natural until browser-native inspectors exist.

## Alternatives

Browser-native workspace UI with per-pane PTYs:

- Better fit for browser-native panes, CodeMirror, inspectors, and mobile.
- More bridge/session complexity.
- Pushes too much topology into the browser/bridge too early.

VS Code/Theia/code-server:

- Huge existing LSP/editor ecosystem.
- Too much inherited platform behavior.
- Harder to make agent feedback and command grammar feel native.

Monaco-only browser IDE:

- Strong editor surface.
- More VS Code-shaped assumptions.
- Less aligned with owning the workspace core.

Multiple bridge-managed PTYs:

- Useful if the browser becomes the real tiling manager.
- Not needed if the C++ parent app owns tiling and child process supervision.
- Adds more reconnection, resize, focus, and metadata complexity to the bridge.
