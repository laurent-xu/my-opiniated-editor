# MVP Roadmap

This is the short execution roadmap. Detailed test expectations live in
[Stage Test Plan](../testing/stage-test-plan.md).

## Phase 0: Build And Test Foundation

Goal: make Bazel and tests available before implementation accelerates.

Status: complete.

Deliverables:

- Bazel workspace/module setup.
- Minimal C++ library and GoogleTest target.
- Fake CLI fixture for future PTY/agent tests.
- A process-boundary integration test using the fake agent.
- Bazel-owned Python/Ruff tooling on NixOS.
- Safe compile database refresh wrapper.

Exit criteria:

- `bazel test //...` passes locally.
- Future `/goal` sessions can add tests without first inventing the harness.
- No placeholder-only targets are required for code that does not exist yet.
- Fixture workspaces for Bazel integration are deferred until the editor starts
  running Bazel against workspaces.

## Phase 1: Parent PTY Bridge Spike

Goal: attach a browser to one parent C++ workspace process.

Status: in progress.

Deliverables:

- Build the browser bridge in C++ around the parent-owned PTY session.
- Minimal C++ parent process that serves a usable shell until the editor exists.
- Parent PTY attach/reconnect integration test.

Exit criteria:

- Browser attaches to the parent process.
- Bridge exposes only one parent PTY.

Current progress:

- `//src/parent:workspace_parent` now execs the user's configured login shell
  in interactive mode.
- `//test/integration:workspace_parent_pty_test` starts the parent under a real
  PTY, sends a shell marker command, verifies shell output, and exits.
- `//src/bridge:parent_pty_session_test` covers the C++ `forkpty` session
  foundation: spawning the shell-backed parent, exchanging bytes, validating
  PTY size errors, and keeping the same parent PID for the session lifetime.
- `//src/bridge:parent_ws_bridge_integration_test` covers the owned C++
  WebSocket bridge: `/health`, `/ws`, terminal input/output, reconnect to the
  same parent PID, and multiple clients.
- The owned bridge now supports multiple WebSocket clients attached to the same
  parent PTY application. The bridge test verifies that an HTTP asset fetch can
  complete while a WebSocket is open and that two WebSocket clients receive the
  same parent output.
- The owned bridge serves `/`, `/client.js`, and `/style.css`; the browser
  client loads xterm.js, connects to `/ws`, and forwards keyboard input/resize.
- The owned bridge can bind to a network interface, but non-loopback binds now
  require `--token` unless an explicit unsafe override is passed.
- Manual browser check on 2026-07-07 confirmed the network page loads, the
  status bar reaches connected, two tabs show the same parent content, and
  refresh works.
- Clipboard support is intentionally parked until HTTPS/reverse-proxy support
  exists.

Still open:

- Decide whether to vendor/package xterm.js instead of loading it from CDN.
- Add dedicated PID and resize integration checks against the shell-backed
  parent PTY.
- Add HTTPS/reverse-proxy setup before reintroducing browser clipboard writes.
- Add browser-level automation once the client grows beyond this static shell.

## Phase 2: Skeleton Workspace

Goal: create the parent app model.

Deliverables:

- Command registry and keymap.
- Pane tree and focus model.
- Session registry.
- Git workspace/worktree metadata.
- Minimal persistence.

Exit criteria:

- Parent app state survives browser reconnect.
- Core model behavior has fast unit tests.

## Phase 3: Terminal, Agent, And Vim Compatibility

Goal: make shell, Codex-style CLIs, and full-screen terminal apps usable before
the built-in editor is ready.

Deliverables:

- Parent-owned child PTYs for shells and agents.
- Multiple child agent sessions.
- Real `fzf` subprocess for agent/session picking.
- Agent status tracking.
- Short-lived `vim` compatibility path.
- Transcript capture.

Exit criteria:

- Shell works from the browser.
- `vim` works as a temporary editing bridge.
- At least two fake/Codex-style agent sessions can run and be found via `fzf`.

## Phase 4: Built-In Editor MVP

Goal: stop relying on `vim` for normal edits.

Deliverables:

- File finder.
- Open/edit/save.
- Basic undo/redo.
- Search in file and workspace.
- Path/location clipboard commands.

Exit criteria:

- Common C++, Python, TypeScript, and markdown edits are practical in the
  built-in editor.
- File edit/save behavior has unit and integration coverage.

## Phase 5: Early Self-Hosting With Bazel

Goal: move development of this editor into the editor itself.

Deliverables:

- Run `bazel test //...` from inside the editor.
- Build/test result view.
- Latest updated files for the active git workspace.
- Interactive diff base picker.
- Workspace diff from selected base.

Exit criteria:

- You can run Bazel, edit files, and run Codex from inside the editor.
- You can pick a base commit and review changed files inside the editor.

## Phase 6: LSP And Diagnostics

Goal: make navigation and diagnostics trustworthy.

Deliverables:

- Server-side LSP client manager.
- clangd, Ruff, Python type checker, and TypeScript language server integration.
- Unified diagnostics finder.
- Definition, references, rename, and code actions.

Exit criteria:

- C++ navigation works through Bazel compile commands.
- Diagnostics can be attached to agent feedback.

## Phase 7: Agent-Native Review

Goal: make plan review and targeted feedback faster than terminal-only use.

Deliverables:

- Full UI and plan-only views for each agent.
- Plan proposal versioning.
- Plan diff latest-vs-previous.
- Section annotations and statuses.
- Focused sub-agent spawn for current file/selection.

Exit criteria:

- You can review and annotate plans section by section.
- You can diff revised plans.
- You can spawn a sub-agent from the current file.

## Phase 8: Hardening

Goal: make the tool dependable for daily use.

Deliverables:

- HTTPS/auth path.
- Browser extension spike for clipboard/shortcut pain.
- Reconnect/recovery hardening.
- Process cleanup and resource limits.
- Backup/export for workspace metadata.

Exit criteria:

- Daily self-hosting survives browser reloads, child process exits, and parent
  app restarts.
