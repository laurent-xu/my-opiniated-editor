# Agent Instructions For Testing Docs

These instructions apply under `plan/testing/`.

## Testing Bias

Testing is a core product requirement. Future implementation goals should move
quickly because the harnesses are strong, not because behavior is manually
rechecked.

Prefer:

- Fast C++ unit tests for pure model behavior.
- Strong integration tests for PTY, process, git, Bazel, browser, and file
  boundaries.
- Fake agents and fake CLIs for deterministic automation.
- Bazel targets as the default test entrypoint.

## Required Coverage Themes

Testing docs should keep coverage for:

- Parent PTY attach/reconnect.
- Child shell and agent PTYs.
- Full-screen TUI compatibility, including short-lived `vim` support.
- Built-in editor file edits and save behavior.
- Bazel self-build and self-test.
- Agent plan extraction, annotation, and plan diffing.
- Git latest-updated-files and diff base selection.
- Sub-agent launch context from current file/selection.
- Clipboard sideband success and failure paths.

## Minimal Implementation Rules

- Do not replace deterministic tests with manual smoke checks.
- Do not add giant snapshots when small assertions cover the behavior.
- Do not require real Codex/Claude credentials for default tests.
- Keep `bazel test //...` useful and reasonably fast.
