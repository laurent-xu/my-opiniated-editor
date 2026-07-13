# Stage Test Plan

This document mirrors the buildout roadmap. Each phase should add tests before
the next phase depends on that behavior.

## Phase 0: Build And Test Foundation

Goal: make testing part of the project shape before implementation accelerates.

Status: complete.

Tests to add:

- Bazel smoke target: `bazel test //...` works on a trivial C++ test.
- C++ unit test binary using GoogleTest.
- Fake CLI script fixture for future agent/process tests.
- Process-boundary test proving a test can run the fake CLI and exchange input.

Exit gate:

- `bazel test //...` succeeds locally.
- Test targets are fast enough to run after small edits.
- Future `/goal` sessions can add real tests without first inventing the build
  harness.
- Phase 0 does not include placeholder-only bridge/browser targets.
- Phase 0 does not include disposable Bazel workspace fixtures; add those when
  testing editor-driven Bazel integration.

## Phase 1: Parent PTY Bridge Spike

Goal: prove the browser can attach to one parent workspace process.

Status: in progress.

Unit tests:

- Sideband handshake accepts matching version.
- Sideband handshake rejects mismatched version with reload-required error.
- Resize message validates rows/cols bounds.

Integration tests:

- Spawn parent app under a PTY and read initial output.
- Bridge forwards browser input bytes to parent PTY.
- Bridge forwards parent PTY output bytes to client.
- Resize event updates parent PTY window size.
- Browser reconnect attaches to the same parent process.

Exit gate:

- Parent PTY attach/reconnect is deterministic under test.
- The bridge still owns only one parent PTY.

Current coverage:

- `//test/integration:workspace_parent_pty_test` spawns the parent app under a
  real PTY, sends keyboard input to the configured shell, observes shell output,
  and exits cleanly.
- `//src/bridge:parent_pty_session_test` verifies the C++ bridge foundation can
  spawn the parent once, exchange bytes, validate PTY size errors, and keep the
  same parent PID for the session lifetime.
- `//src/bridge:parent_ws_bridge_integration_test` starts the owned C++ bridge,
  connects to `/health` and `/ws`, verifies terminal I/O, reconnects, observes
  the same parent PID, and verifies multiple clients see the same output.
- `//src/bridge:parent_ws_bridge_integration_test` verifies browser-refresh
  shape by fetching HTTP assets while one WebSocket remains open.
- `//src/bridge:parent_ws_bridge_integration_test` verifies two simultaneous
  WebSocket clients attached to the same parent PTY receive the same output.
- The same integration test verifies the owned bridge serves the thin browser
  client assets and that the JS includes xterm.js setup and `/ws` connection.
- The same integration test verifies token-protected HTTP/WebSocket attach and
  rejects non-loopback binds without a token or explicit unsafe override.
- Manual browser testing confirmed the network page loads, status reaches
  connected, two tabs show the same parent content, and refresh works.
- Clipboard is parked until HTTPS/reverse-proxy support exists.
- Dedicated shell-backed PID and resize checks are deferred to their own focused
  integration tests.

## Phase 2: Skeleton Workspace

Goal: make the parent app's internal model testable before it gains features.

Unit tests:

- Command registry lookup and dispatch.
- Keymap resolution and modal state.
- Pane tree split/focus/swap/promote.
- Session registry create/focus/archive transitions.
- Workspace metadata and selected diff base persistence.
- Persistence serialization for simple workspace state.
- Clipboard command request construction.

Integration tests:

- Start parent app, create panes through keyboard commands, reconnect, and
  verify layout state survives.
- Send sideband clipboard/status messages while parent app is active.

Exit gate:

- Core workspace state can change safely under fast unit tests.
- Reconnect does not require browser-owned workspace state.

## Phase 3: Terminal, Agent, And Vim Compatibility

Goal: make shells, `vim`, and CLI agents usable before the built-in editor is
ready.

Unit tests:

- Child session registry state transitions.
- Agent status derivation and manual override.
- Pane focus and split logic with child PTYs.
- Key routing between parent UI and focused child PTY.
- Transcript capture buffering.
- Agent finder ranking/filtering by status.

Integration tests:

- Run a shell in a parent-owned child PTY.
- Run `vim` in a child PTY, enter insert mode, write text, save, and quit.
- Verify alternate-screen programs do not corrupt parent UI state.
- Start two fake agent CLIs in separate child PTYs.
- Send feedback text to the focused fake agent.
- Capture transcripts from both fake agents.
- Jump between fake agents through the finder.
- Kill one child PTY without killing the parent app.

Exit gate:

- You can use the workspace to run shell commands, `vim`, and multiple agent
  CLIs from the browser.
- `vim` is treated as a short-lived compatibility path, not the editor strategy.

## Phase 4: Built-In Editor MVP

Goal: replace `vim` for normal editing as soon as possible.

Unit tests:

- Buffer load/save.
- Insert/delete ranges.
- Undo/redo.
- Selection movement.
- Search within buffer.
- File finder ranking.
- Dirty-file tracking.

Integration tests:

- Open a file, edit it, save it, and verify bytes on disk.
- Open two files in split panes and switch focus without data loss.
- Use built-in editor to modify a small source file, then run Bazel test.

Exit gate:

- Normal small edits can be done in the built-in editor without falling back to
  `vim`.

## Phase 5: Early Self-Hosting With Bazel

Goal: move development of this editor into the editor itself.

Unit tests:

- Command palette routes build/test commands.
- Bazel target label parsing.
- Build/test result model.
- Failure location parsing for common compiler errors.
- Compile database staleness detection.
- Git status parsing.
- Diff base candidate ranking.
- Latest updated files model.

Integration tests:

- From inside the parent app, run `bazel test //...` for this repo or a close
  fixture.
- Open the first test failure from parsed output.
- Edit a source file with the built-in editor and rerun the affected test.
- Select a base commit and show changed files from that base.
- Show latest updated files after a fake agent modifies files.
- Start at least two fake agents and compare their transcripts/plans.
- Use one fake agent to propose an edit while another runs a review-like
  workflow.

Exit gate:

- The editor can build/test itself from inside its own UI.
- The editor can orchestrate multiple CLI-agent sessions well enough to help
  with its own development.

## Phase 6: LSP And Diagnostics

Goal: make code navigation and diagnostics trustworthy.

Unit tests:

- LSP message framing.
- LSP request/response correlation.
- Diagnostic normalization.
- Code action representation.
- Bazel compile database staleness detection.

Integration tests:

- Start clangd against a tiny Bazel C++ fixture.
- Open definition in a fixture file.
- Surface a compile error as a diagnostic.
- Run Ruff on a Python fixture and normalize diagnostics.
- Attach a diagnostic to an agent feedback draft.

Exit gate:

- C++ and Python diagnostics appear in the same finder.
- Diagnostics are usable as agent feedback context.

## Phase 7: Agent-Native Review

Goal: make plan review and targeted feedback faster than terminal-only use.

Unit tests:

- Plan heading and checklist parsing.
- Stable plan section ID generation.
- Plan proposal versioning and plan diff generation.
- Section status transitions.
- Comment and feedback draft storage.
- Transcript range linking.
- Sub-agent launch context from current file.

Integration tests:

- Fake agent emits a multi-section plan.
- Fake agent emits a revised plan; parent app shows diff from previous plan.
- Parent app extracts sections and displays them in a plan review pane.
- User marks one section needs-change and sends targeted feedback.
- Feedback appears in the agent child PTY.
- User spawns a focused sub-agent for the current file.
- Plan comments survive parent app restart.

Exit gate:

- You can review an agent plan section by section and send precise feedback.

## Phase 8: Hardening

Goal: make the tool dependable enough for daily use.

Tests to add:

- Long-running reconnect soak test.
- Child process cleanup on parent shutdown.
- Crash/restart recovery for metadata DB.
- Browser reload does not spawn duplicate parents.
- Auth/session rejection tests.
- Clipboard permission failure path.
- Worktree-per-agent workflow test.

Exit gate:

- The common daily workflow remains intact across reloads, restarts, and child
  process failures.
