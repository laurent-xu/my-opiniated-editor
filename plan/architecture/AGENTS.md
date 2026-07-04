# Agent Instructions For Architecture Docs

These instructions apply under `plan/architecture/`.

## Architecture Bias

- Parent C++ workspace app owns the important model.
- Browser bridge exposes one primary parent PTY. Prefer a strong existing OSS
  bridge if it fits; if building custom, use C++.
- Shells and agents are parent-owned child PTYs.
- JSON sideband is for browser-specific capabilities, not the main workspace
  command plane.
- Version the sideband at handshake only.
- Keep the parent PTY stream as raw terminal bytes.

## State Ownership

Architecture docs should keep these owned by the parent app:

- Pane tree and focus.
- Command registry and keymaps.
- Editor state.
- Agent sessions and transcripts.
- Plan proposal versions and plan diffs.
- Git workspace metadata, selected diff base, and file activity.
- LSP/build/test diagnostics.
- Clipboard requests before browser execution.

## Minimal Implementation Rules

- Do not introduce a distributed state model before reconnect demands it.
- Do not add binary protocols before JSON is measurably insufficient.
- Do not add browser-owned process topology.
- Prefer explicit process boundaries and logs over hidden magic.
