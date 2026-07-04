# Bazel Setup Plan

## Goal

Set up Bazel in Phase 0 so implementation work starts with a real build and
test harness. The first useful milestone is not a complete build system; it is
being able to run:

```text
bazel test //...
```

and have that command cover the first C++ unit tests plus placeholder targets
for future bridge/browser/integration tests.

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
bridge/
  BUILD.bazel
web/
  BUILD.bazel
test/
  fixtures/
    BUILD.bazel
  integration/
    BUILD.bazel
```

Keep the first layout boring. It only needs to support small C++ libraries,
tests, fixture scripts, and later bridge/browser targets.

## Phase 0 Targets

Create these targets first:

```text
//src/core:core
//src/core:core_test
//src/parent:workspace_parent
//src/parent:workspace_parent_test
//bridge:bridge_placeholder_test
//web:web_placeholder_test
//test/fixtures:fake_agent
//test/integration:pty_placeholder_test
```

The placeholder tests should be real tests that pass and can later be replaced
or extended. Avoid empty targets that teach future agents nothing.

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
- Bazel fixture harness: run Bazel against a tiny fixture workspace.

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
