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

## Browser Bridge

Run once from a shell:

```bash
tools/bridge/run_bridge.sh
```

For boot startup and manual restart, see [tools/bridge/README.md](tools/bridge/README.md).
