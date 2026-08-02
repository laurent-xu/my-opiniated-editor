---
name: manual-test
description: Prepare this repository for manual browser testing by rebasing the current worktree onto the latest origin/main, resolving all rebase conflicts, and rebuilding and restarting the bridge service pair on HTTPS port 7683. Use when Codex needs to update a feature worktree from main and deploy that exact worktree to the port-7683 manual-test instance.
---

# Manual Test

Work from the repository root. Complete the rebase before restarting the
services so port 7683 always runs the rebased worktree.

## Rebase Onto `origin/main`

1. Inspect the current branch and worktree:

   ```bash
   git branch --show-current
   git status --short --branch
   ```

   Require a named feature branch and a clean worktree before starting. Do not
   discard, stash, or commit unrelated user changes to make the worktree clean.
   If a rebase is already in progress, resume its conflict-resolution workflow
   instead of starting another rebase.

2. Update the remote-tracking branch and start the rebase:

   ```bash
   git fetch origin main
   git rebase origin/main
   ```

3. If the rebase stops, resolve every conflict rather than aborting merely
   because conflicts exist:

   - Inspect `git status --short` and
     `git diff --name-only --diff-filter=U`.
   - Read each conflicted file and the replayed commit before editing. When
     useful, inspect stages with `git show :1:path`, `git show :2:path`, and
     `git show :3:path`.
   - Remember that during a rebase, `ours`/stage 2 is the updated
     `origin/main` side and `theirs`/stage 3 is the feature commit being
     replayed. Do not select a side wholesale based only on those labels.
   - Preserve the upstream behavior and the feature commit's intent wherever
     they are compatible. Resolve add/add, rename, and modify/delete cases by
     understanding both histories; remove all conflict markers.
   - Stage only the paths resolved in this pass. Use `git add -- <paths>` for
     retained files and `git rm -- <paths>` only when deletion is the intended
     merged result.
   - Continue non-interactively with `GIT_EDITOR=true git rebase --continue`.
     Repeat until the rebase completes. Do not use `git rebase --skip` unless
     inspection proves the replayed commit is already fully present upstream.

4. Verify the result:

   ```bash
   git status --short --branch
   git merge-base --is-ancestor origin/main HEAD
   ```

   Require the ancestry check to succeed and no rebase or unresolved conflict
   to remain. If conflict resolution changed behavior, inspect the final diff
   and run the relevant Bazel gate through `$run-bazel-tests` before deploying.

## Restart Port 7683

Restart the bridge from the rebased worktree. The helper builds the C++ bridge
and parent binaries before restarting the HTTP bridge and HTTPS proxy:

```bash
manual_test_root="$(git rev-parse --show-toplevel)"
cd "$manual_test_root"
tools/bridge/restart_bridge.sh 7683 "$manual_test_root"
```

Verify both units after the helper succeeds:

```bash
systemctl --user is-active my-opiniated-editor-bridge@7683.service
systemctl --user is-active my-opiniated-editor-bridge-https@7683.service
```

Report the rebased branch and HEAD commit, which conflicts were resolved, and
whether both port-7683 services are active. Give the user the manual-test URL
as `https://<server-ip>:7683/`; do not claim readiness if the build, restart,
or service checks failed.
