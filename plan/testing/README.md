# Testing Plan

The goal is to move fast without turning the workspace into a fragile personal
tool. Tests should cover the parts that are hardest to debug manually:

- PTY byte forwarding.
- Terminal compatibility with full-screen programs.
- Child PTY lifecycle for shells and agents.
- Clipboard sideband behavior.
- Reconnect and resize.
- Command routing.
- File editing correctness.
- Bazel self-build/self-test loops.
- LSP and diagnostics normalization.
- Agent transcript and plan extraction.

## Documents

- [Testing Guidelines](testing-guidelines.md)
- [Stage Test Plan](stage-test-plan.md)
- [Integration Scenarios](integration-scenarios.md)
- [Goal Session Test Contract](goal-session-test-contract.md)

## Default Test Command

The project should get to this shape early:

```text
bazel test //...
```

That command should run fast unit tests and stable integration tests. Slower or
more environment-sensitive tests should use Bazel tags so they can be included
deliberately.

For implementation goals, use the [Goal Session Test Contract](goal-session-test-contract.md).
