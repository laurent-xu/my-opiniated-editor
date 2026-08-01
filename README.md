# My Opiniated Editor

Browser-served, keyboard-first development workspace.

## Setup

Required tools on NixOS:

```bash
nix shell nixpkgs#bazelisk nixpkgs#clang-tools
```

Install the local hook:

```bash
install -m 755 tools/git-hooks/pre-commit .git/hooks/pre-commit
```

## Commands

```bash
bazel --batch test //...
tools/bazel/refresh_compile_commands.sh
bazel run //tools/python:pyformat -- --check .
```

## Bazel Cache

The checked-in `.bazelrc` stores compiled actions in
`~/.cache/my-opiniated-editor/bazel/disk`. Bazel shares this content-addressed
cache across all worktrees while keeping each worktree's output base separate,
so concurrent builds remain independent. Bazel garbage-collects the cache in
the background when it grows beyond 10 GiB.

## Browser Bridge

Run once from a shell:

```bash
tools/bridge/run_bridge.sh 7682
```

For boot startup and manual restart, see [tools/bridge/README.md](tools/bridge/README.md).
