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
bazel run //:refresh_compile_commands
```
