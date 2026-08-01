# Git Workspace Model

## Purpose

The editor should treat a git workspace or worktree as a first-class object.
This is required for parallel agents, diff review, updated-file views, and
self-hosting work.

## Repository And Worktree Registry

Each C++ parent instance owns a registry of repositories and worktrees tracked
by that editor instance. The bridge gives the parent an explicit state
directory. The standard port-keyed runner persists its protobuf at:

```text
$XDG_STATE_HOME/my-opiniated-editor/instances/port-<port>/worktrees.pb
```

When `XDG_STATE_HOME` is unset, use
`$HOME/.local/state/my-opiniated-editor/instances/port-<port>/worktrees.pb`.
Different bridge ports therefore do not share repository registrations.

The registry stores canonical absolute repository-root and worktree paths.
Mutable metadata such as branch names, HEAD, remotes, and availability comes
from Git when needed.

The registry starts empty. Startup must not automatically register the
repository containing the editor's initial working directory. Repository
registration is an explicit user action in the worktree management overlay.

Registry writes replace the complete protobuf atomically. Write and `fsync` a
temporary file in the destination directory, rename it over the registry, then
`fsync` the directory. A failed update must leave the previous registry intact,
and invalid registry data must not be silently overwritten.

Clearing an in-session tray is a process-lifecycle action and does not change
Git or this registry. Removing a tracked worktree is a separate parent-owned
workflow: force-remove it through the registered repository's bare Git
directory, prune stale Git metadata when its directory is already missing,
force-delete its reported local branch, atomically unregister it, then destroy
any in-session tray. Detached worktrees have no branch to delete. If Git already
no longer lists the worktree, unregistering it is idempotent. If Git still lists
the worktree after a failed removal, preserve both the registry entry and the
in-session tray so the operation can be retried.

The parent process's startup directory is a protected worktree. The service
starts in `%h/my-opiniated-editor/main`, whose built parent and bridge binaries
remain in use for the service lifetime, so removal attempts must fail before
Git or the registry is changed.

See [Worktree Tray Plan](../implementation/worktree-tray-plan.md) for the
milestone order and interaction details.

## Workspace Metadata

Each workspace should track:

- Workspace ID.
- Repo root.
- Worktree path.
- Branch.
- Current HEAD.
- Selected diff base commit.
- Primary agent session ID, if any.
- Sub-agent session IDs.
- Dirty files.
- Recently updated files.
- Last build/test status.
- Last refresh time.

This metadata belongs in the parent app's workspace model and persistence, not
in the browser.

## File Activity

The editor should maintain a file activity index per workspace:

- Path.
- Change kind: modified, added, deleted, renamed, untracked.
- Last observed mtime.
- Last agent session that touched it, if known.
- First seen dirty timestamp.
- Last seen dirty timestamp.
- Owning Bazel target, if known.
- Diagnostics count, if known.

Initial implementation can refresh from `git status --porcelain=v1` and file
mtimes. Later versions can use filesystem notifications for faster updates.

## Diff Base Selection

The selected diff base is workspace-specific. It should default conservatively:

1. Agent session start commit, if reviewing an agent workspace.
2. Merge-base with the configured main branch, if known.
3. Previous commit.

The user should be able to select another base interactively through a fuzzy
commit picker.

Base commit candidates:

- Recent commits.
- Merge-base with main.
- Tags.
- Agent session start commits.
- Manually entered revisions.

Changing the base commit should refresh:

- Workspace diff.
- Latest updated files.
- Agent changed-files summary.
- Review prompt context.

## Agent To Workspace Relationship

Default relationship:

- One primary agent per git worktree.
- Zero or more manually spawned sub-agents per workspace.
- Sub-agents are scoped to a file, selection, diagnostic, target, or plan
  section.

The workspace should make this visible:

```text
workspace: feature/foo
  primary agent: codex-123 working
  sub-agents:
    claude-456 reviewing src/core/buffer.cc
    codex-789 proposing tests for //src/core:buffer_test
```

## Commands

- `workspace.find`
- `workspace.open`
- `workspace.latest_updated_files`
- `workspace.refresh_git_status`
- `workspace.select_diff_base`
- `workspace.show_diff_from_base`
- `workspace.copy_diff_from_base`
- `workspace.show_agent_file_activity`
- `workspace.open_primary_agent`
- `workspace.spawn_subagent_for_current_file`

## Implementation Notes

Start with polling and explicit refresh commands. Avoid deep git abstraction
until the workflow proves itself.

Early commands can shell out to git:

- `git status --porcelain=v1`
- `git diff --name-status <base>`
- `git diff <base> -- <path>`
- `git merge-base HEAD main`
- `git log --oneline --decorate -n 100`

Use structured parsing for porcelain/status output. Avoid parsing decorative
human diff output except where showing it directly to the user.
