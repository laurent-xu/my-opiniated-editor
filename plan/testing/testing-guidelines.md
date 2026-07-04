# Testing Guidelines

## Testing Philosophy

Prefer many small deterministic tests plus a few strong integration tests that
exercise real process boundaries. This project is mostly dangerous at the
boundaries: PTYs, WebSockets, browser clipboard, terminal escape handling, agent
CLIs, Bazel subprocesses, and file writes.

The test suite should make it cheap to change internals while catching broken
workflows quickly.

## Test Tiers

Use Bazel tags to keep tiers clear:

- `unit`: no network, no browser, no real external tools except test binaries.
- `integration`: real local processes, PTYs, sockets, temp repos, and files.
- `browser`: browser automation against the thin web client.
- `manual`: tests that need credentials, real agent CLIs, or clipboard approval.
- `slow`: useful but not part of the normal edit/test loop.

Recommended commands:

```text
bazel test //...
bazel test //... --test_tag_filters=integration
bazel test //... --test_tag_filters=browser
bazel test //... --test_tag_filters=-slow,-manual
```

## Unit Tests

Unit tests should cover pure behavior:

- Command registry lookup and argument validation.
- Keymap resolution, modal state, and conflict detection.
- Pane tree split/focus/swap/promote logic.
- Session registry state transitions.
- Clipboard request construction.
- Sideband JSON encode/decode.
- Sideband handshake compatibility.
- Terminal escape parsers owned by the app.
- Plan section parsing.
- Diagnostic normalization.
- Bazel label parsing and target ownership heuristics.
- File buffer edit transactions, undo/redo, selections, and save behavior.

For C++, use GoogleTest unless there is a strong reason not to. It is common,
works well with Bazel, and keeps the choice boring.

## Integration Tests

Integration tests should exercise real boundaries:

- Spawn the parent app under a PTY and assert it draws a usable initial screen.
- Attach the bridge to the parent PTY and send real input bytes.
- Resize the browser-side PTY and verify the parent app receives the new size.
- Drive a parent-owned child shell.
- Run a full-screen terminal program such as `vim` as a compatibility bridge.
- Run multiple fake agent CLIs in child PTYs.
- Capture agent transcripts and parse plans from them.
- Request clipboard writes through the sideband.
- Restart/reconnect browser sessions without losing parent app state.
- Run Bazel build/test commands against a temp fixture repo.

Use fake CLIs for automated agent tests. Real Codex/Claude tests should be
manual or opt-in because credentials, latency, and output formats are unstable.

## Browser Tests

Browser tests should verify the thin host, not the whole editor model:

- xterm.js renders nonblank terminal output.
- Keyboard input reaches the parent PTY.
- Resize events reach the bridge.
- Clipboard sideband requests call the browser clipboard path when permission is
  available.
- Reconnect shows useful state and reattaches to the parent process.
- Browser reload does not create duplicate parent sessions.

Use Playwright once there is a real browser host. Keep these tests few and
workflow-focused.

## Golden Tests

Golden tests are useful for:

- Plan parsing from transcripts.
- Diagnostic normalization from Bazel/clangd/Ruff output.
- Terminal escape parsing if the app owns parser behavior.
- Command palette ranking fixtures.

Do not use golden snapshots for large terminal screens unless the output is
stable and intentionally reviewed. Screen snapshots become noisy quickly.

## Test Data

Prefer hermetic fixture workspaces:

- A tiny Bazel C++ project.
- A tiny Python project.
- A mixed C++/Python/web project.
- Fake agent scripts that emit known plans, diffs, and command output.
- Fake LSP server for protocol edge cases.

Fixtures should live in the repo and be small enough to understand at a glance.

## Flake Policy

A flaky test is a broken test. If a test depends on timing:

- Prefer explicit readiness messages over sleeps.
- Use timeouts only as failure bounds.
- Capture logs on failure.
- Make child process cleanup part of the test harness.
- Keep ports and temp directories isolated per test.

## CI And Local Use

Early local command:

```text
bazel test //...
```

Later CI shape:

- Presubmit: unit and stable integration tests.
- Nightly or manual: browser, slow, and real-agent smoke tests.
- Release gate: self-hosting scenario plus browser reconnect/clipboard checks.

