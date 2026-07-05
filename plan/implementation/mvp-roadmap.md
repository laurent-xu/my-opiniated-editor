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

- Evaluate existing OSS bridge first, starting with `ttyd`.
- If building the bridge, build it in C++.
- Minimal C++ parent process drawing a basic terminal screen.
- Clipboard path through OSC 52 or a small sideband.
- Parent PTY attach/reconnect integration test.

Exit criteria:

- Browser attaches to the parent process.
- Bridge exposes only one parent PTY.
- Clipboard write request reaches the browser.

Current progress:

- `//src/parent:workspace_parent` now enters a small TTY command loop when run
  under a terminal.
- `//test/integration:workspace_parent_pty_test` starts the parent under a real
  PTY, reads the initial screen, sends `status`, and exits with `quit`.

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
