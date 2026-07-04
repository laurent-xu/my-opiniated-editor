# Vision and Principles

## Product Statement

This is a personal development workspace served from Linux and operated from a
browser on macOS, iPhone, or Linux. It should become the primary interface for
programming, agent supervision, shell work, planning, editing, diagnostics, and
build/test loops.

The product is not "a terminal in a browser" and not "VS Code in a browser".
It is a keyboard-native workspace whose central objects are workspaces, panes,
files, symbols, Bazel targets, diagnostics, agent sessions, plans, plan
sections, plan proposal versions, diffs, git workspaces, updated files, and
feedback.

## Non-Negotiables

- Keyboard-only operation is a first-class product constraint.
- AI agent sessions are first-class workspace objects, not anonymous terminal
  tabs.
- Multiple parallel agents, agent status views, full UI mode, plan-only mode,
  and plan diffs are core coding workflows.
- Plan review must be structured enough to comment on individual sections,
  approve or reject steps, attach evidence, and send targeted feedback.
- Git workspaces must support latest-updated-file review and interactive diff
  base selection.
- Clipboard writes must target the browser client's clipboard, not the Linux
  server clipboard.
- The Linux server is the source of truth for files, processes, language
  servers, build tools, agent CLIs, and persistence.
- The browser client should reconnect cleanly to the parent workspace process
  and rebuild any thin overlay state from server state.
- Bazel is the planned build system for this project, and the editor should
  eventually be good at building and testing itself.
- Strong automated test harnesses are required early so future `/goal` sessions
  can move quickly without breaking PTY, process, editor, or agent behavior.
- NixOS is the current development environment, not a product requirement for
  the editor's first version.

## What To Own

Own the parts that directly shape daily speed:

- Command registry and command grammar.
- Keymap system, including multi-key chords and modal contexts.
- Workspace/session graph.
- Fuzzy finding indexes and ranking.
- Editor command model and eventually the document model.
- Agent session registry, transcript capture, and feedback routing.
- Plan parser, plan section IDs, comments, approvals, and feedback state.
- Plan proposal versioning and plan diffing.
- Git workspace metadata, file activity, and selected diff base state.
- LSP client coordination, diagnostics normalization, and code action routing.
- Bazel target/test/build integration, especially for self-hosting.
- Project environment launch policy for tools and subprocesses.
- Clipboard command protocol between server and browser.

## What To Rent

Rent the parts that are important but not the core differentiator:

- Browser runtime and DOM.
- WebSocket transport.
- PTY library.
- xterm.js for the parent workspace terminal surface.
- CodeMirror 6 only as a later optional browser-native editor/view adapter.
- Browser extension APIs for persistent clipboard permission.
- TLS/reverse proxy/auth building blocks.

## Product Shape

The first screen should be the workspace itself:

- A parent C++ workspace/editor app rendered through the browser terminal.
- A tiling pane grid inside that parent app.
- One focused object at a time.
- A command palette reachable from everywhere.
- A narrow status surface for current workspace, branch, build state, agent
  state, diagnostics count, and pending clipboard permissions.
- No landing page, marketing layer, or mouse-dependent navigation.

## Scope Boundaries

Do not build these in the first version:

- VS Code extension compatibility.
- A general cloud IDE platform.
- Multi-user collaboration.
- A full browser extension before clipboard pain is proven.
- A custom browser editor/rendering engine before the parent workspace model is
  validated.
- A general plugin system before internal extension points settle.
