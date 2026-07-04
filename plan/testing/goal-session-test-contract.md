# Goal Session Test Contract

Future `/goal` sessions should treat tests as part of the requested work, not as
cleanup after the implementation.

## Default Contract

Every implementation goal should answer these before coding:

- What behavior is being protected?
- What is the smallest useful unit test?
- What integration boundary can fail?
- What fixture or fake process is needed?
- What Bazel command proves the work?

Every goal should finish with:

- Files changed.
- Tests added or updated.
- Exact test commands run.
- Any tests intentionally skipped and why.
- Known residual risk.

## Definition Of Done

A goal is not done unless one of these is true:

- It added or updated tests that would fail without the implementation.
- It was purely documentation/planning.
- It explains why automated testing is not practical yet and adds a test-harness
  task to the plan.

For this project, "manual smoke tested in the browser" is useful but not enough
for core behavior. Manual checks should supplement automated tests, not replace
them.

## Required Harnesses

The first implementation goals should establish these harnesses:

- Bazel C++ unit test harness.
- Bridge/PTY integration harness.
- Fake parent app for bridge tests.
- Fake child CLI/agent scripts.
- Browser host smoke harness.
- Tiny Bazel fixture repo.
- Clipboard sideband fake browser/client.

Once a harness exists, new features should extend it instead of inventing a
separate ad hoc test setup.

## Agent Instructions

When using coding agents, the prompt should include:

```text
Add focused unit tests and the relevant integration test for the behavior you
change. Run the narrow test first, then run the broad Bazel target that covers
the area. Report exact commands and failures.
```

For risky boundary work, the prompt should be more specific:

```text
This touches PTY/process/browser behavior. Add or update an integration test
that runs real local processes and fails if reconnect, resize, input forwarding,
or cleanup breaks.
```

## No-Test Exceptions

Acceptable exceptions:

- Planning documents.
- README-only edits.
- Pure comments with no behavior change.
- Temporary spike code that is explicitly marked for deletion before the next
  phase.

Unacceptable exceptions:

- "Too hard to test" for PTY, process, command, buffer, or protocol behavior.
- "Will test manually later" for behavior that should be deterministic.
- Adding command handlers without command tests.
- Adding process lifecycle behavior without cleanup tests.

