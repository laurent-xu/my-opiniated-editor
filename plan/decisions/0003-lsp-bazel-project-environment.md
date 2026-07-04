# Decision 0003: LSP, Bazel, And Project Environment

Status: Proposed

## Decision

Run language servers and build/lint/test tools server-side in the project's
current build environment. Make the C++ parent workspace app the LSP client and
the owner of normalized diagnostics.

Initial tooling:

- C++: clangd, clang-format, clang-tidy.
- C++ compile database: Bazel-generated `compile_commands.json`.
- Python: Ruff plus basedpyright or pyright.
- Web: TypeScript language server plus project-local lint/format tasks.
- Build: Bazel for this project.
- Environment: inherited from the shell/dev environment that starts the parent
  app.

## Context

The workspace is running on a Linux server. The browser may be macOS, iPhone, or
Linux, but it should not need local dev tools. LSP servers and build tools need
direct access to the server filesystem and the same environment that builds the
project.

Bazel complicates C++ LSP because clangd needs compile commands. NixOS is the
current development environment, but it is not itself a core editor feature.
Treat it as local setup until a concrete workflow needs deeper integration.

## Consequences

Benefits:

- Browser reconnects do not restart language intelligence.
- Diagnostics can be shared with agents and plan feedback.
- Build failures, lint warnings, and LSP diagnostics can live in one finder.
- The editor can build and test itself through Bazel.
- Bazel target knowledge becomes a workspace primitive.

Costs:

- LSP client implementation is real work.
- Bazel compile command refresh needs care.
- Environment changes may require manual process restarts at first.
- Tool versions need to be visible enough for debugging.

## Implementation Notes

- Start with the smallest useful LSP subset.
- Do not build a generic IDE protocol before clangd works.
- Use Hedron's compile command extractor first.
- Add owned Bazel `aquery` or Build Event Protocol integration later.
- Store Bazel version and compile database timestamp in workspace metadata.
- Add explicit Nix/dev-shell tracking only if inherited environment behavior
  becomes a real source of bugs.
