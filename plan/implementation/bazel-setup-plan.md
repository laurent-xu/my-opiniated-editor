# Bazel Setup Plan

## Goal

Set up Bazel in Phase 0 so implementation work starts with a real build and
test harness. The first useful milestone is not a complete build system; it is
being able to run:

```text
bazel test //...
```

and have that command cover this project's first C++ unit tests plus a small
process-boundary test.

## Initial Repo Shape

Proposed early layout:

```text
MODULE.bazel
.bazelrc
src/
  parent/
    BUILD.bazel
  core/
    BUILD.bazel
test/
  fixtures/
    BUILD.bazel
  integration/
    BUILD.bazel
```

Keep the first layout boring. It only needs to support small C++ libraries,
tests, fixture scripts, and integration tests. Add bridge/browser Bazel
packages when there is bridge/browser code to build.

## Phase 0 Targets

Create these targets first:

```text
//src/core:core
//src/core:core_test
//src/parent:workspace_parent
//src/parent:workspace_parent_test
//test/fixtures:fake_agent
//test/fixtures:fake_agent_test
//test/fixtures:python_runtime_test
//test/integration:fake_agent_process_test
```

Do not create placeholder-only tests for future bridge or browser work. A target
should exercise current code, a reusable fixture, or a concrete contract that is
already implemented.

## C++ Test Framework

Use GoogleTest for C++ unit tests unless a later implementation reason changes
that decision.

Why:

- Common with Bazel C++ projects.
- Good enough for unit and small integration tests.
- Familiar failure output.
- Easy for coding agents to extend.

## Test Tags

Use consistent Bazel tags from the start:

```text
unit
integration
browser
slow
manual
requires-network
requires-credentials
```

Default `bazel test //...` should avoid tests that require credentials, real
agent CLIs, browser permissions, or network access.

## First Integration Harnesses

Add these as soon as the corresponding code exists:

- Parent PTY harness: spawn `workspace_parent` under a PTY and exchange bytes.
- Bridge harness: connect a test client to the bridge and verify byte
  forwarding.
- Fake agent harness: run two deterministic fake CLI agents in child PTYs.
- `fzf` harness: feed stable IDs to `fzf` or a test double and assert selected
  IDs route correctly.
- `vim` compatibility harness: run `vim` in a temp directory, write a file, and
  quit.
- Bazel fixture harness: defer until the editor starts running Bazel against
  workspaces, then add a tiny disposable workspace fixture.

The `vim` test is a compatibility gate, not a long-term editing strategy. The
built-in editor should get its own tests immediately after it exists.

## `.bazelrc` Direction

Early `.bazelrc` should optimize for readable local output:

```text
test --test_output=errors
test --keep_going
build --color=yes
test --color=yes
```

Keep remote caching, sandbox tuning, and large performance settings out until
there is a concrete need.

## Agent Instructions

When a `/goal` session modifies behavior, it should prefer adding a Bazel test
target over adding an ad hoc script. If a new harness is needed, add it under
Bazel first, then implement the feature against that harness.

## Phase 0 Exit Criteria

- `bazel test //...` passes.
- C++ GoogleTest works.
- The repo has a place for PTY and fake-agent integration tests.
- The testing docs name the next harnesses to build.
- Future implementation goals can report exact Bazel commands.
