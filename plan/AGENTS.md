# Agent Instructions For Planning Docs

These instructions apply under `plan/`.

## Planning Style

- Keep plans concrete enough that a future `/goal` session can execute them.
- Prefer phase exit criteria, tests, and observable behavior over vague goals.
- Separate product intent, architecture, implementation, decisions, and tests.
- When a recommendation is uncertain, record the tradeoff and state the current
  bias.
- Keep NixOS framed as the current development environment, not a product
  feature, unless the user changes that direction.

## Required Consistency Checks

When editing planning docs:

- If phases change, update `plan/testing/stage-test-plan.md`.
- If build setup changes, update `plan/implementation/bazel-setup-plan.md`.
- If agent workflow changes, update `plan/product/agent-coding-workflow.md` and
  `plan/testing/integration-scenarios.md`.
- If git workspace behavior changes, update
  `plan/architecture/git-workspace-model.md`.
- If protocol behavior changes, update
  `plan/architecture/browser-bridge-protocol.md`.

## Minimal Implementation Rules

- Do not add a new plan file if a short section in an existing file is clearer.
- Do not introduce generic platforms, plugin systems, or multi-client features
  unless they are needed for a named milestone.
- Keep the parent C++ app as the owner of core workflow state.
- Keep browser-specific responsibilities thin and explicit.
