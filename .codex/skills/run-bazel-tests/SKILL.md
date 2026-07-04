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

The repo is used on NixOS. Before preserving or changing Bazel's Python
configuration, verify the active system interpreter:

```bash
which python3
```

`.bazelrc` should point `build --python_path` at that discovered interpreter,
for example:

```text
startup --output_user_root=/tmp/my-opiniated-editor-bazel-cache
build --noincompatible_use_python_toolchains
build --python_path=<output from which python3>
```

Keep those settings unless replacing them with an explicit Nix-pinned Python
toolchain. They avoid Bazel downloading a generic Linux Python that cannot run
on NixOS because its dynamic loader path is not available.

Prefer adding or updating Bazel test targets over ad hoc scripts. Keep default
`bazel test //...` fast and free of credentials, real agent CLIs, browser
permissions, and network requirements.
