# Project Build Runtime

## Core Position

Bazel matters because this editor should be able to build and test itself. NixOS
matters because it is the current development environment. Do not turn Nix into
an editor feature until there is a concrete workflow that needs it.

The first runtime goal is simple:

- Set up Bazel early enough that `bazel test //...` is the normal confidence
  command before feature work accelerates.
- Launch the parent C++ workspace app from the same environment used to build
  the repo.
- Let child tools inherit that environment.
- Integrate Bazel enough to build, test, and navigate failures in this project.

## Environment-Bound Processes

These processes need the project build environment:

- The parent C++ workspace app.
- Parent-owned shell panes.
- Parent-owned agent CLIs.
- LSP servers.
- Bazel commands.
- Linters and formatters.
- Test runners.

MVP assumption: the user starts the service from the right shell/dev
environment. The app records tool versions but does not need a full environment
manager.

## Process Launcher

Create a launcher abstraction in the C++ parent app:

```text
launch(process_kind, argv, cwd, env_overrides)
```

The launcher resolves:

- Working directory.
- Extra environment variables.
- Parent PTY, child PTY, or non-PTY mode.
- Restart policy.
- Log destination.

Do not over-design environment descriptors early. Add explicit project
environment metadata after real failures show what needs tracking.

## Bazel Self-Hosting

The workspace should understand enough Bazel to work on itself:

- `.bazelversion`, if used.
- `.bazelrc`.
- `MODULE.bazel`.
- `WORKSPACE`, if still present.
- Packages and BUILD files.
- C++ binary/library/test targets.
- Bazel output base.
- Last build/test status.

Initial Bazel setup should happen in Phase 0. It does not need full editor
integration yet, but it should establish the build graph, unit test targets, and
integration test targets that future `/goal` sessions can extend. The concrete
setup plan lives in [Bazel Setup Plan](../implementation/bazel-setup-plan.md).

Useful commands:

- Show active Bazel version.
- Show active Bazel flags for this workspace.
- Open `.bazelrc`.
- Refresh target index.
- Refresh compile commands.
- Build current target.
- Test current target.
- Open last build failure.
- Open last BEP build event file.

## Compile Commands

clangd needs a compile database for C++ code. Start with an existing Bazel
compile command extractor, then revisit ownership later.

MVP behavior:

- Generate or refresh `compile_commands.json`.
- Record when it was generated.
- Warn when BUILD, MODULE, `.bazelrc`, or toolchain files changed after the last
  refresh.

## Current NixOS Use

For this repo, a Nix dev shell may be the practical way to provide:

- C++ compiler and standard library toolchain.
- Bazel or Bazelisk.
- buildifier/buildozer.
- clangd.
- clang-format.
- clang-tidy.
- Python.
- Ruff.
- basedpyright or pyright.
- Node.js.
- TypeScript tooling.
- Git.
- Ripgrep.

This is setup guidance, not a product commitment. The editor does not need a Nix
UI or Nix workflow until the build starts demanding it.

## Cache Policy

Keep caches explicit enough to debug:

- Bazel disk cache.
- Bazel repository cache.
- clangd index.
- workspace metadata DB.
- agent transcripts.
- fuzzy index.

Avoid hiding large caches under surprising directories. A command should
eventually show cache paths and sizes per workspace.

## Service Deployment

MVP:

```text
enter project build environment
workspace-server
```

Later:

- Package the bridge/core as a Bazel-built binary.
- Add a `systemd --user` service if useful.
- Optionally expose through Tailscale and Caddy.
- Keep secrets outside the repo.

The browser client remains disposable. The server processes and metadata are the
important persistent pieces.
