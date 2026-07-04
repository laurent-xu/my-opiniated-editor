# Agent Instructions For Implementation Plans

These instructions apply under `plan/implementation/`.

## Implementation Plan Requirements

Every implementation plan should include:

- Phase or milestone.
- Deliverables.
- Exit criteria.
- Tests or harnesses to add.
- Exact default Bazel command when known.
- What is intentionally out of scope.

## Current Buildout Priorities

Keep this order unless the user changes it:

1. Bazel and test foundation.
2. Parent PTY bridge spike: evaluate OSS bridge first; build C++ only if needed.
3. Skeleton parent workspace model.
4. Terminal, agent, and short-lived `vim` compatibility.
5. Built-in editor MVP.
6. Early self-hosting with Bazel.
7. LSP and diagnostics.
8. Agent-native review.
9. Hardening.

## Minimal Implementation Rules

- Do not plan future-proof infrastructure before a phase needs it.
- Do not split milestones just to create more documents.
- Prefer fake deterministic processes for early integration tests.
- Keep implementation plans runnable by future `/goal` sessions.
