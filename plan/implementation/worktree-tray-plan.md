# Worktree Tray Plan

## Goal

Add persistent worktree-backed trays while preserving the parent-PTY-first
architecture. The C++ parent owns repository registration, worktree creation,
picker overlays, durable state, and tray lifecycle. The browser and bridge only
forward user actions.

The current implementation already provides:

- One browser-visible parent rendering PTY.
- One content PTY and terminal screen model per tray.
- Nine anonymous trays selected through escape mode.
- Worktree tray identities and shells rooted in a selected worktree.
- A persistent protobuf registry and per-tray repository-management overlays.
- A parent-owned fzf picker for switching to available tracked worktrees.

Each milestone below produces one commit. After its tests pass, stop for manual
review before starting the next milestone.

## Shortcuts

- `Esc`, `Shift+1` through `Shift+9`: switch anonymous trays.
- `Esc`, `Shift+T`: switch to an available tracked worktree.
- `Esc`, `Shift+W`: toggle the active tray's worktree management overlay.

The worktree management overlay ultimately provides:

- Add repository.
- Create worktree.

Opening an interactive overlay leaves command mode so typing, arrows, and Enter
are routed to the overlay. `Esc` continues to toggle command mode without
closing the overlay. From command mode, `Shift+W` closes the repository manager
and `Shift+T` cancels the worktree picker.

The repository manager and worktree picker are mutually exclusive on the
visible surface. Invoking `Shift+W` while the picker is open replaces it with
the repository manager; invoking `Shift+T` while the repository manager is open
replaces it with the picker.

Each tray owns its worktree-management overlay state. Switching trays hides the
previous tray's overlay without destroying it; switching back redraws the tray
and its overlay with the input and cursor preserved. Helper PTYs owned by
inactive overlays continue being drained.

## Persistent Registry

Persist a global per-user protobuf registry at:

```text
$XDG_STATE_HOME/my-opiniated-editor/worktrees.pb
```

When `XDG_STATE_HOME` is unset, use:

```text
$HOME/.local/state/my-opiniated-editor/worktrees.pb
```

The registry stores only stable identity:

```proto
syntax = "proto3";

message WorktreeRegistry {
  uint32 format_version = 1;
  repeated Repository repositories = 2;
}

message Repository {
  string root_path = 1;
  repeated Worktree worktrees = 2;
}

message Worktree {
  string path = 1;
}
```

Repository and worktree paths are canonical absolute paths. Branch names,
default branches, remote URLs, and availability are queried from Git because
they can change outside the editor.

The registry starts empty. The editor must not register the repository
containing its startup directory automatically. Repositories enter the registry
only through an explicit worktree-management action.

### Atomic Writes

Every registry update must:

1. Serialize the complete next registry.
2. Create a unique temporary file in the registry directory.
3. Write all bytes and `fsync` the temporary file.
4. Close the temporary file.
5. Atomically replace `worktrees.pb` with `rename`.
6. `fsync` the containing directory.

Keeping the temporary file beside the destination ensures the rename stays on
one filesystem. A failure before rename leaves the previous registry intact.
A missing registry loads as empty. Invalid or unsupported data must produce a
visible error and must not be silently overwritten.

## Milestone 6: Protobuf Registry

Deliverables:

- Add a direct Bazel protobuf dependency and generated C++ target.
- Add `WorktreeRegistryStore` in separate source and header files.
- Resolve the XDG state path with the documented fallback.
- Load, validate, normalize, and atomically save the registry.
- Keep persistence separate from `TrayManager`.
- Inject a temporary registry path in tests.

Tests:

- Missing registry loads as empty.
- Protobuf round trip with multiple repositories and worktrees.
- Duplicate and non-canonical paths are rejected or normalized consistently.
- Corrupt, truncated, and unsupported registry data is not overwritten.
- A failed update leaves the previous registry readable.
- Temporary and destination files use the same directory.

Exit criteria:

- Registry tests pass.
- The full Bazel test suite passes.
- One commit is ready for manual review.

## Milestone 7: Register Or Create A Repository

Deliverables:

- Add `Esc`, `Shift+W` and a parent-owned, per-tray worktree management overlay.
- Initially expose one complete action: Add repository.
- Ask for the repository-root path first.
- If the path is an existing valid bare-root repository, register it.
- If the path is new or empty, ask for a clone URL and create the bare root.
- Discover live worktrees from `git worktree list --porcelain`.
- Persist the repository and discovered worktrees atomically.

The created layout follows the bare-root worktree model:

```text
repository-root/
|-- .bare/
`-- .git
```

Creating a repository must:

- Clone the remote as bare into `.bare`.
- Write `.git` so it points to `./.bare`.
- Configure fetching of remote branches.
- Fetch the remote.
- Verify that Git exposes a default branch.
- Register the repository without creating an initial worktree.

If Git succeeds but persistence fails, report the partial success. A later Add
repository action must be able to adopt that valid existing bare root without
cloning it again.

Tests:

- Register an existing valid bare root and its worktrees.
- Reject a path that is not a supported repository root.
- Create and register a new bare root from a local fake remote.
- Validate destination, cancellation, clone failure, fetch configuration, and
  default-branch detection.
- Verify partial-success recovery after a persistence failure.

Exit criteria:

- A fresh editor can manually register its first repository.
- A new repository can be created without creating a worktree.
- The full Bazel test suite passes.
- One commit is ready for manual review.

## Milestone 8: Switch Existing Worktrees

Deliverables:

- Replace the `Shift+T` placeholder with a parent-owned `fzf` overlay.
- Build candidates from available worktrees in the persistent registry.
- Validate candidates with `git worktree list --porcelain`.
- Exclude bare, prunable, missing, and untracked entries.
- Switch through the existing `TrayManager::switch_to_worktree` path.
- Reuse an existing tray and shell when a worktree is already open.

While the picker is visible, tray PTYs continue being drained into their screen
models, but their output is not painted over the picker. Selection or
cancellation ends with a complete redraw of the selected or previous tray.

Tests use fake `git` and `fzf` processes to cover selection, cancellation,
filtering, input routing, redraw, and stable tray identity.

Exit criteria:

- `Esc`, `Shift+T` opens and switches among manually registered worktrees.
- Canceling does not switch trays or leak Escape to the content PTY.
- The full Bazel test suite passes.
- One commit is ready for manual review.

## Milestone 9: Create A Worktree

Deliverables:

- Add Create worktree to the `Shift+W` management overlay.
- Select a registered repository.
- Ask for a new branch name.
- Resolve the repository's default branch from Git.
- Derive a worktree path below the repository root, replacing branch-name
  slashes with hyphens.
- Allow the user to review or edit that path before confirmation.
- Create the branch and worktree with direct process arguments.
- Atomically add the new worktree to the registry.
- Open its worktree tray immediately.

The operation is equivalent to:

```text
git worktree add -b <branch> <path> <default-branch>
```

The editor must not guess between `main` and `master`. Missing or ambiguous
default-branch metadata is an actionable error.

If Git succeeds but persistence fails, keep the Git worktree, report the
partial success, and allow it to be adopted during a later management action.

Tests:

- Default-branch resolution.
- Branch-name and path validation.
- Derived paths for branch names containing slashes.
- Duplicate branches and existing destinations.
- Git and persistence failures.
- Immediate switch into the new worktree tray.

Exit criteria:

- `Esc`, `Shift+W` handles both repository addition and worktree creation.
- New worktrees always branch from the repository default branch.
- The full Bazel test suite passes.
- One commit is ready for manual review.

## Reconciliation Rules

When the registry or a selector is opened:

- Validate registered roots using Git.
- Compare persisted worktrees with `git worktree list --porcelain`.
- Keep missing persisted paths but mark them unavailable.
- Detect live Git worktrees not yet tracked by the editor.
- Never silently delete persisted repository or worktree entries.

`Shift+T` shows only available tracked worktrees. Adoption and removal actions
beyond the recovery behavior described above are out of scope for these
milestones.

## Architecture Boundary

```text
browser
  -> action message
bridge
  -> parent control command
workspace parent
  -> shared Overlay lifecycle, command-mode routing, and pane redraw
worktree workflow
  -> registry/live-Git candidate intersection and validation
worktree registry store
  -> protobuf loading and atomic persistence
tray manager
  -> running tray and content-PTY lifecycle
```

The repository manager and worktree picker implement the same `Overlay`
interaction contract. The repository manager owns its small form editor. The
worktree picker owns a temporary fzf PTY and terminal screen because fzf handles
its own query text and arrow input. Command mode remains above both overlays, so
entering it never leaks Escape into either child surface.

The picker terminal is constrained to a bottom pane. Its bytes are interpreted
by a dedicated terminal screen and redrawn only inside that region; full-screen
fzf control sequences cannot erase the tray behind it. The picker does not own
trays, Git state, or persistence, which keeps it suitable for later pane-level
selectors.

Command mode, active tray identity, and active overlay identity are parent-owned
session state. The bridge broadcasts parent status to every attached browser;
the browser captures keys and renders status but does not independently mutate
or infer that shared state.

## Verification Gate

For every milestone:

1. Add focused unit or deterministic process-boundary tests.
2. Run the narrow relevant Bazel targets.
3. Run `bazel --batch test //...`.
4. Create exactly one milestone commit.
5. Stop for manual review before continuing.
