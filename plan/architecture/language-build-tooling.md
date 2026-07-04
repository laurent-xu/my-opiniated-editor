# Language and Build Tooling

## Core Position

The C++ parent workspace app should be the LSP client and should render the
primary LSP experience inside its own UI. The browser should not own
language-server processes, protocol state, or diagnostics state.

Reasoning:

- Language servers need server-side filesystem access.
- They must run inside the correct project environment.
- Diagnostics should survive browser reconnects because they live in the parent
  app's model.
- Build, lint, test, and LSP diagnostics should share one normalized model.
- Agent sessions need to reference diagnostics and code actions without asking
  the browser to mediate.

## C++ Tooling

Primary stack:

- `clangd` for LSP.
- `clang-format` for formatting.
- `clang-tidy` for deeper checks, either through clangd or explicit tasks.
- Bazel-generated `compile_commands.json` for clangd.

Initial compile database path:

- Use Hedron's Bazel compile commands extractor for the first implementation.
- Store the active compile database path in workspace metadata.
- Expose a command to refresh it.
- Warn when BUILD/MODULE/.bazelrc files changed after the last refresh.

Later compile database path:

- Build an owned extractor from Bazel `aquery` and/or Build Event Protocol
  output.
- Make target-scoped compile databases possible for very large repos.

Useful C++ commands:

- Open definition.
- Open declaration.
- Find references.
- Rename symbol.
- Apply code action.
- Format selection/file.
- Run clang-tidy for file or target.
- Find Bazel owner target for current file.
- Build current target.
- Test current target.

## Python Tooling

Primary stack:

- Ruff for linting and formatting.
- basedpyright for strict type checking and LSP, or pyright if upstream
  compatibility matters more than stricter defaults.
- pytest as the default test runner when Bazel does not own tests.

The project environment should provide the Python runtime, Ruff, type checker,
and pytest version. If a project uses Bazel for Python, prefer Bazel targets for
test execution and keep non-Bazel Python commands as convenience tasks.

## Web Tooling

Primary stack:

- TypeScript language server for TypeScript/JavaScript LSP.
- Project-local formatter and linter.
- Browser app built with TypeScript and a small bundler.

Do not let web tooling become the workspace architecture. The browser client is
a thin terminal host and clipboard broker; the parent C++ workspace model
remains primary.

## Bazel Integration

Bazel should be a first-class workspace object source for this project because
the editor should be able to build and test itself.

The workspace should index:

- Workspace root.
- `MODULE.bazel`, `WORKSPACE`, and `.bazelrc`.
- Packages and BUILD files.
- Targets.
- Target kind.
- Test targets.
- Owner target for file.
- Reverse dependencies.
- Last build/test status.

Useful Bazel commands:

- Query targets under current package.
- Find tests related to current file.
- Build current target.
- Test current target.
- Test affected targets.
- Run current binary target.
- Open last build failure.
- Copy target label.
- Open target BUILD definition.
- Refresh compile database.

For build/test output, prefer structured output where possible. Build Event
Protocol output should become the long-term source for test status, logs, and
failure navigation.

## Project Environment

Every workspace should have an explicit process environment, but this is an
implementation concern before it is an editor feature.

For this repo, the practical assumption is NixOS. Use a Nix dev shell if it is
the easiest way to keep the C++ compiler, Bazel, clangd, Python tooling, and
frontend tooling reproducible.

Initial approach:

- Launch the parent app from the same environment used to build the repo.
- Let the parent app inherit that environment for clangd, Bazel, linters, and
  test runners.
- Record tool versions for debugging.
- Add explicit environment descriptors later only if implicit inheritance
  becomes confusing.

Avoid turning Nix into an editor product feature before there is a concrete need.

## Unified Diagnostics

Normalize diagnostics from:

- LSP servers.
- Bazel builds.
- Bazel tests.
- Ruff.
- clang-tidy.
- pytest.
- TypeScript.
- Agent review comments.

Common fields:

- Workspace.
- Source.
- Severity.
- File/range.
- Message.
- Related locations.
- Suggested actions.
- Owning Bazel target, if known.
- Owning git workspace and diff base, if relevant.
- First seen and last seen timestamps.

The diagnostics finder should be one of the primary navigation tools.
