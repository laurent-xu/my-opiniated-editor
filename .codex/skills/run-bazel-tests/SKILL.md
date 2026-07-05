---
name: run-bazel-tests
description: Run and triage Bazel tests for this repo. Use when Codex needs to verify changes, perform a completion audit, choose between targeted and full Bazel test commands, investigate Bazel test failures, add new tests under Bazel, or explain this repo's NixOS/Bazel Python test setup.
---

# Run Bazel Tests

## Workflow

Work from the repo root: `/home/notmyfoo/my-opiniated-editor`.

Use this default confidence gate before claiming implementation work is complete:

```bash
bazel --batch test //...
```

Use targeted tests during the edit loop when the changed area is clear:

```bash
bazel --batch test //src/core:core_test
bazel --batch test //src/parent:workspace_parent_test
bazel --batch test //test/integration:fake_agent_process_test
```

If the right target is unclear, inspect `BUILD.bazel` files or run:

```bash
bazel --batch query //...
```

## Failure Triage

For a failing target, rerun only that target with fuller output:

```bash
bazel --batch test --test_output=all //path/to:target
```

Then inspect the Bazel test log only if the command output is insufficient.
Report the exact command, failing target, and first actionable error.

If the failure is from sandboxed network or cache access, rerun the same
`bazel --batch test ...` command with escalation rather than changing the build
or bypassing Bazel.

## Repo Constraints

The repo is used on NixOS. Bazel should not point at the ambient system Python
with `build --python_path`. Python test targets use the Nix-provided Bazel
toolchain registered in `MODULE.bazel`, and Python formatting uses the
Nix-provided Ruff target:

```bash
bazel --batch run //tools/python:pyformat -- --check .
```

Do not switch back to `rules_python`'s downloaded standalone CPython on NixOS;
that generic Linux interpreter cannot run without the expected dynamic loader.

Refresh `compile_commands.json` with the repo wrapper, not `bazel run`; Hedron's
generated script calls Bazel internally and can deadlock behind the outer
`bazel run` client lock:

```bash
tools/bazel/refresh_compile_commands.sh
```

Prefer adding or updating Bazel test targets over ad hoc scripts. Keep default
`bazel test //...` fast and free of credentials, real agent CLIs, browser
permissions, and network requirements.
