# System Architecture

## High-Level Shape

```text
Browser client
  thin TypeScript host
  one xterm.js terminal surface
  optional status/control overlay
        |
        | HTTPS + WebSocket
        v
Bridge server
  serves browser assets
  authenticates browser sessions
  exposes one primary PTY byte stream
  forwards resize/input/output
        |
        | parent PTY + optional control sideband
        v
C++ parent workspace/editor app
  command registry
  keymap resolution
  tiling and pane model
  workspace/session graph
  git workspace model
  terminal renderer
  file index
  editor state and edits
  LSP client manager
  Bazel/build integration
  agent session manager
  plan version and diff manager
  child PTY manager for shells/agents
  persistence
        |
        +--> child shell and agent CLIs via internal PTYs
        +--> language servers
        +--> bazel, git, linters, tests, project environment tools
```

## Main Architecture Position

Serve one parent C++ workspace/editor process through one externally bridged
PTY. The parent app is the real workspace. The browser is the transport,
terminal renderer, and optional control/status host.

Reasoning:

- You want to own the productivity surface in C++, including tiling, editing,
  sessions, and agent supervision.
- A single parent PTY keeps the bridge simple and avoids making the browser
  responsible for process/session topology.
- Agent plans and plan sections need stable IDs and comments.
- Diagnostics should be objects that link to files, symbols, targets, and
  actions.
- Fuzzy finding across workspaces, sessions, targets, and plan sections is a
  structured-data problem.
- Those objects should exist inside the parent workspace model, not only as
  scraped terminal text.
- Browser-native panes can be added later for specific inspector views, but
  they should not drive the first architecture.

## Core Daemon Responsibilities

The C++ parent workspace app owns:

- Workspace discovery.
- Command execution.
- Session lifecycle.
- Git workspace/worktree metadata.
- Diff base selection and changed-file tracking.
- Tiling and pane focus.
- File and symbol indexing.
- Editor state and edit transactions.
- Terminal rendering for the main workspace UI.
- Child PTY lifecycle for shells and agent CLIs.
- LSP process lifecycle and protocol handling.
- Build/test/lint task execution.
- Agent session lifecycle.
- Plan parsing and feedback state.
- Plan proposal versioning and plan diffing.
- Persistence.

The parent app may expose a typed control sideband to the bridge for status,
auth/session metadata, future HTTPS clipboard support, and future
browser-native inspector views. The bridge should not contain business logic
beyond connection handling, parent PTY forwarding, resize, and browser-specific
plumbing.

## Browser Client Responsibilities

The browser owns:

- Rendering the parent terminal stream through xterm.js.
- Keyboard event capture.
- Passing keyboard input to the parent app.
- Local transient UI state.
- Resize events.
- Optional status/control overlay.

The browser should be able to reconnect to the parent PTY and rebuild any small
overlay state from the server. Avoid making browser memory the only copy of
important workspace state.

## Persistence

Use files as the source of truth for source code. Use SQLite or a small embedded
store for workspace metadata:

- Workspaces.
- Pane layouts.
- Agent sessions.
- Git workspaces.
- Selected diff bases.
- File activity.
- Transcript indexes.
- Plan sections and comments.
- Plan proposal versions.
- Recent commands.
- Diagnostic snapshots.
- Fuzzy-finder history.

Keep append-only logs for agent/session events. They are easier to inspect,
replay, and debug than opaque mutable blobs.

## IPC Between Bridge and Core

Start with a local WebSocket or Unix-domain-socket JSON protocol. Move to a
binary schema only after message volume or schema drift justifies it.

Suggested message classes:

- Commands.
- State snapshots.
- State patches.
- LSP events.
- Task events.
- Agent events.
- Git workspace events.
- Clipboard requests.
- Parent PTY attach metadata.

## Process Model

Run one parent workspace app per active workspace or per Unix user on the Linux
server. The bridge attaches the browser to the parent app's PTY. The parent app
spawns child processes under the current project environment.

For isolation later:

- Put agent sessions in separate git worktrees.
- Track per-workspace environment hashes.
- Restart language servers when the project environment or Bazel config changes.
- Add resource limits for long-running agents and builds.
