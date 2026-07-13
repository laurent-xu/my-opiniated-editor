# Start Here

This is the operating summary for future `/goal` sessions. Use the detailed
docs as references, not as required pre-reading.

## Current Direction

- Build an owned C++ parent workspace/editor app.
- Serve it through one primary browser PTY.
- Shells, agents, panes, editor state, git workspace state, plans, and
  diagnostics are owned by the parent app.
- The browser bridge is plumbing: terminal rendering, WebSocket, auth, and
  reconnect. Clipboard is parked until HTTPS is in place.
- Use Bazel early. `bazel test //...` should become the default confidence
  command.

## Current Phase

Phase 1: parent PTY bridge spike.

Already done:

- Bazel module setup with C++ GoogleTest targets.
- Nix-provided Bazel Python toolchain and Bazel-owned Ruff formatter.
- Deterministic fake CLI fixture for future process, PTY, and agent tests.
- Process-boundary fake-agent integration test.
- Parent app currently execs the user's configured login shell so the browser
  serves a usable shell while the real editor is still being built.
- C++ `ParentPtySession` using `forkpty`, with tests for shell I/O, strong
  parent process identity, and PTY size validation errors.
- Minimal owned C++ WebSocket bridge with `/health` and `/ws`, tested for
  reconnect to the same parent PID.
- The owned bridge now separates browser socket lifetime from parent app
  lifetime: one parent PTY can be served by multiple simultaneous WebSockets,
  and browser reload can fetch assets while an older WebSocket is still open.
- Owned bridge serves a thin browser client at `/` using xterm.js, `/ws`, and
  resize frames.
- Network binding for the owned bridge requires `--token` unless an explicit
  unsafe override is passed.
- Clipboard support was removed from Phase 1 after manual HTTP testing showed
  the network path should be stabilized before adding browser clipboard writes.
  Reintroduce clipboard only after HTTPS/reverse proxy support is available.
- Manual network browser check on 2026-07-07: the browser loaded the parent
  prompt, status showed connected, two tabs showed the same parent content, and
  refresh worked against the same running bridge.
- Safe `compile_commands.json` refresh wrapper:
  `tools/bazel/refresh_compile_commands.sh`.

Next implementation goals:

1. Decide whether to vendor/package xterm.js instead of loading it from CDN.
2. Add dedicated PID and resize integration checks against the shell-backed
   parent PTY.
3. Add HTTPS/reverse-proxy setup before reintroducing clipboard.
4. Add browser-level automation once the client grows beyond this static shell.

## First Self-Hosting Milestone

The first meaningful self-hosting milestone is:

- Run the editor from the browser.
- Run `bazel test //...` from inside the editor.
- Edit files in the built-in editor.
- Run Codex inside a parent-owned agent pane.
- Jump to that Codex session from the agent finder.

`vim` support is only a short-lived compatibility bridge before the built-in
editor is usable.

## Bridge Policy

- Current owned bridge foundation:
  `bazel test //src/bridge:parent_pty_session_test`.
- Current owned bridge reconnect check:
  `bazel test //src/bridge:parent_ws_bridge_integration_test`.
- Current thin browser client check:
  `bazel test //src/bridge:parent_ws_bridge_integration_test`.
- Current network-bind guard check:
  `bazel test //src/bridge:parent_ws_bridge_integration_test`.
- Build the bridge in C++ around the parent-owned PTY session. Keep the browser
  bridge thin and avoid adding terminal-server dependencies unless the owned
  bridge becomes the wrong abstraction.

## Agent Workflow Policy

- One primary agent per git worktree.
- Sub-agents are manually spawned for focused work on the current file,
  selection, diagnostic, target, or plan section.
- Use a real `fzf` subprocess for session/file/commit pickers early.
- Keep the selected result as a structured ID in the parent app.

## Test Policy

Every implementation goal should add or update tests unless it is docs-only.

Default command:

```text
bazel test //...
```

Manual compile database refresh:

```text
tools/bazel/refresh_compile_commands.sh
```

For PTY, process, browser, file, git, or Bazel boundaries, add an integration
test or update the missing harness in the test plan.

## Non-Goals For Now

- VS Code extension compatibility.
- Browser-owned process topology.
- Multiple bridge-owned PTYs.
- Rich Nix UI/workflows.
- General plugin system.
- Browser-native editor architecture before the parent app needs it.
