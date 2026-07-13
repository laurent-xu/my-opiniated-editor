# Agent Instructions

These instructions apply to the whole repo. More specific `AGENTS.md` files in
subdirectories override or extend this file.

## Hard Direction

- Parent workspace/editor app is C++.
- Browser serves one primary parent PTY.
- Browser bridge is plumbing. If using OSS, evaluate the strongest maintained
  option first; if building it, use C++.
- Parent app owns panes, child PTYs, editor state, agent sessions, git
  workspace state, LSP/build state, plans, and feedback.
- Bazel and tests come early. `bazel test //...` should become the default
  confidence command.

## Execution Rules

- Start with [plan/START_HERE.md](plan/START_HERE.md).
- Add or update tests for implementation work.
- Use the repo-local `$run-bazel-tests` skill when running, selecting, or
  triaging Bazel test commands.
- Prefer Bazel targets over ad hoc scripts.
- Use the narrowest practical Bazel visibility. Prefer package-private targets;
  make targets public only when there is a concrete cross-package API.
- Model system libraries as named Bazel targets in `third_party/system` instead
  of putting raw linker flags directly on product/library targets.
- Use fake CLIs/processes for deterministic PTY and agent tests.
- Keep docs and code ASCII unless a file already requires otherwise.
- Preserve the parent-PTY-first architecture unless the user explicitly changes
  direction.

## Karpathy-Inspired Taste

- Make the simplest complete thing work end to end before widening scope.
- Prefer understandable code paths over clever abstractions.
- Keep state visible, inspectable, and easy to print.
- Build tight feedback loops: run the narrow test, then the relevant broad test.
- Own the important model; rent plumbing that is not core to productivity.

## C++ Coding Rules

- Prefer small strong types for process IDs, file descriptors, ports, sizes,
  and other raw platform values when they cross function or class boundaries.
- Put reusable low-level helper types in `src/base`; keep feature directories
  for feature-specific types only.
- Pass non-trivial inputs by `const&` unless the callee needs ownership or a
  deliberate copy.
- Keep raw OS values at syscall boundaries; convert to named types at repo
  APIs.

## Minimal Implementation Gate

Before adding an abstraction, protocol, framework, service, option, or plugin
surface, ask:

- Is this needed for the current phase exit criteria?
- Does this need to exist at all?
- Is there existing code in this repo to reuse?
- Does the standard library or native platform already do this?
- Is there an installed dependency that already solves it cleanly?
- Can this be represented as data plus one command?
- Can one direct line or one small function solve it?
- Would deleting this make the system easier to understand?

Only then write the minimum custom solution that works. Be lazy about the
solution, never about understanding the code path first.

Do not cut validation, data-loss handling, security, accessibility, or tests to
make code smaller.

Avoid early:

- General plugin systems.
- Multiple bridge-owned PTYs.
- Browser-native editor architecture before the parent app needs it.
- Rich Nix workflow features before the build actually demands them.
- Binary protocols before JSON sideband messages become a bottleneck.
