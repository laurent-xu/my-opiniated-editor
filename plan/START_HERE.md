# Start Here

This is the operating summary for future `/goal` sessions. Use the detailed
docs as references, not as required pre-reading.

## Current Direction

- Build an owned C++ parent workspace/editor app.
- Serve it through one primary browser PTY.
- Shells, agents, panes, editor state, git workspace state, plans, and
  diagnostics are owned by the parent app.
- The browser bridge is plumbing: terminal rendering, WebSocket, clipboard,
  auth, and reconnect.
- Use Bazel early. `bazel test //...` should become the default confidence
  command.

## Current Phase

Phase 0: build and test foundation.

Next implementation goals:

1. Set up Bazel with a minimal C++ library and GoogleTest target.
2. Add placeholder-but-real test targets for bridge, browser host, fixtures, and
   integration tests.
3. Add deterministic fake CLI fixtures for future PTY and agent tests.

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

- If an existing open-source bridge fits, use the strongest maintained option
  rather than building bridge plumbing.
- Current first candidate: `ttyd`, because it is purpose-built for terminal over
  web, has broad adoption, and is native.
- Evaluate WeTTY if `ttyd` does not fit the xterm.js/control-overlay path.
- Avoid choosing GoTTY solely for star count; it appears much less maintained.
- If we build the bridge ourselves, build it in C++ to stay aligned with the
  parent app and Bazel.

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

For PTY, process, browser, file, git, or Bazel boundaries, add an integration
test or update the missing harness in the test plan.

## Non-Goals For Now

- VS Code extension compatibility.
- Browser-owned process topology.
- Multiple bridge-owned PTYs.
- Rich Nix UI/workflows.
- General plugin system.
- Browser-native editor architecture before the parent app needs it.

