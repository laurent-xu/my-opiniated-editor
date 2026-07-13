# Integration Scenarios

These scenarios should become automated tests or documented manual checks. They
represent the workflows that matter most.

## Parent PTY Attach

Scenario:

1. Start bridge.
2. Start or attach parent C++ workspace app under one PTY.
3. Connect browser/client.
4. Verify initial screen renders.
5. Send keyboard input.
6. Verify parent app receives input.

Assertions:

- Only one parent PTY is exposed by the bridge.
- Parent process survives browser disconnect.
- Reconnect attaches to the same parent process.

Current automated coverage:

- `//test/integration:workspace_parent_pty_test` starts the C++ parent under a
  real PTY, sends keyboard input to the configured shell, observes shell output,
  and exits cleanly.
- `//src/bridge:parent_pty_session_test` verifies the owned C++ PTY session
  keeps one parent PID alive while clients come and go in the future bridge.
- `//src/bridge:parent_ws_bridge_integration_test` verifies the owned C++ bridge
  keeps the same parent PID across WebSocket reconnect.
- `//src/bridge:parent_ws_bridge_integration_test` verifies the bridge can serve
  HTTP assets while a WebSocket is open, matching browser refresh behavior.
- `//src/bridge:parent_ws_bridge_integration_test` verifies two simultaneous
  WebSocket clients can attach to one parent PTY and receive the same output.
- `//src/bridge:parent_ws_bridge_integration_test` verifies the bridge serves
  the thin browser client assets.
- `//src/bridge:parent_ws_bridge_integration_test` verifies token-protected
  HTTP/WebSocket attach and rejects non-loopback binds without a token or
  explicit unsafe override.
- Manual browser check on 2026-07-07 confirmed the network page loads, status
  reaches connected, two tabs show the same parent content, and refresh works.
- Clipboard is intentionally parked until HTTPS/reverse-proxy support exists.

Deferred focused coverage:

- Verify resize propagation against the shell-backed parent PTY.
- Verify process identity with a dedicated PID check rather than temporary
  parent-app commands.

## Full-Screen Terminal Compatibility

Scenario:

1. Open a parent-owned terminal pane.
2. Start `vim` in a child PTY.
3. Enter insert mode.
4. Type text.
5. Save and quit.
6. Verify file contents.

Assertions:

- Alternate screen mode works.
- Resize works while `vim` is open.
- Escape/meta keys reach the child PTY correctly.
- Parent pane focus and layout survive child process exit.

This is an early compatibility milestone only. The built-in editor should
replace `vim` for normal editing quickly.

## Multiple CLI Agents

Scenario:

1. Start two fake agent CLIs in separate parent-owned child PTYs.
2. Each fake agent emits a different plan.
3. Parent app captures both transcripts.
4. User switches between agent panes with keyboard commands.
5. User sends targeted feedback to one agent.

Assertions:

- Agent sessions have separate IDs and transcripts.
- Agent sessions can be filtered by status.
- Agent sessions can be opened through the finder.
- Feedback goes to the selected agent only.
- Killing one agent does not disturb the other.
- Plans remain discoverable after pane focus changes.

Real Codex/Claude CLI tests should be manual or opt-in at first.

## Agent Plan Views And Diffs

Scenario:

1. Fake agent emits an initial plan.
2. User opens the agent in plan-only view.
3. User annotates one plan section.
4. Fake agent emits a revised plan.
5. User opens a diff between latest and previous plan.
6. User sends feedback about only the changed section.

Assertions:

- Full UI view and plan-only view refer to the same agent session.
- Plan versions are stored separately.
- Section-level annotations survive plan revisions.
- Plan diff can identify changed sections or falls back to text diff.
- Feedback is sent to the selected agent session.

## Git Workspace Review

Scenario:

1. Create a temp git workspace with several commits.
2. Start one fake primary agent for that workspace.
3. Fake agent modifies, adds, and deletes files.
4. User opens latest updated files view.
5. User interactively selects a base commit.
6. User opens workspace diff from that base.

Assertions:

- Latest updated files match git status.
- File activity is associated with the agent session when known.
- Base commit selection updates workspace diff.
- Selected base commit persists for the workspace.
- Changed files are navigable from keyboard.

## Sub-Agent For Current File

Scenario:

1. Open a file in the built-in editor.
2. Put cursor on a function or select a range.
3. Trigger spawn-sub-agent shortcut.
4. Fake sub-agent receives file path, selection/range, workspace ID, and selected
   diff base.

Assertions:

- Sub-agent is linked to the current workspace.
- Sub-agent is marked as a sub-agent, not the primary workspace agent.
- Launch context includes current file and available diagnostics.
- Primary agent remains unchanged.

## Future Clipboard From Server To Browser

Status: parked until HTTPS/reverse-proxy support exists.

Do not add clipboard integration tests during Phase 1. Once the browser path
runs in a secure context, add tests around the concrete protocol chosen at that
time.

## Bazel Self-Build

Scenario:

1. Open this repo in the parent app.
2. Run `bazel test //...` from inside the workspace.
3. Parse build/test status.
4. Open first failure if present.

Assertions:

- Bazel command uses the current project environment.
- Output is captured and searchable.
- Failures can be linked back to files or targets.
- The same command can run from the command palette.

## Built-In Editor Self-Edit

Scenario:

1. Open a source file in the built-in editor.
2. Make a small edit.
3. Save.
4. Run the relevant Bazel test.
5. Revert or make a second edit.

Assertions:

- File bytes on disk match expected output.
- Dirty state clears only after a successful save.
- Undo/redo works before save.
- Build/test command sees saved contents.

## Browser Reload

Scenario:

1. Open workspace in browser.
2. Start shell, fake agent, and editor panes.
3. Reload browser.
4. Reattach to parent app.

Assertions:

- Parent app survives reload.
- Child sessions remain listed.
- The bridge does not spawn a duplicate parent process.
- Clipboard/status sideband reconnects cleanly.

## LSP Diagnostics

Scenario:

1. Open a tiny C++ Bazel fixture.
2. Start clangd.
3. Introduce a compile error.
4. Observe diagnostic.
5. Fix error and observe diagnostic clearing.

Assertions:

- Diagnostic ranges are stable.
- Diagnostics link to files and Bazel targets when known.
- Diagnostics can be copied or attached to agent feedback.
