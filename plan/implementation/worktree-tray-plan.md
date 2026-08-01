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
- Anonymous tray shells rooted in the configured user's home directory.
- Worktree tray identities and shells rooted in a selected worktree.
- A persistent protobuf registry and per-tray repository-management overlays.
- One parent-owned worktree overlay with fzf selectors for tracked worktrees and
  registered repositories.

Each milestone below produces one commit. After its tests pass, stop for manual
review before starting the next milestone.

## Shortcuts

- `Esc`, `Shift+1` through `Shift+9`: switch anonymous trays.
- `Esc`, `Shift+W`: toggle the active tray's worktree overlay.
- In command mode while the Worktrees picker is visible, `Shift+C`: ask for
  `y/N` confirmation, then clear the highlighted tray without changing Git or
  the persistent registry.
- In command mode while the Worktrees picker is visible, `Shift+R`: ask for
  `y/N` confirmation, then purge and unregister the highlighted worktree. For
  an anonymous tray, this behaves like `Shift+C` because there is no persistent
  worktree to remove.

The worktree overlay provides three modes, in this order:

- Worktrees: switch to an available tracked worktree. This is the default.
- Add worktree: select a registered repository and create or adopt a worktree.
- Add repository: register an existing bare root or create a new one.

`Tab` and `Shift+Tab` cycle directly through those modes. Worktrees and the
repository-selection step of Add worktree use the same fzf-backed path picker,
so their complete candidate lists remain visible and searchable. One shared
parent-rendered mode footer stays on the bottom row while either the fzf dialog
or a form dialog is drawn independently above it. Changing modes resets every
mode to its initial selector or form. The Worktrees picker also includes every
anonymous tray used in the current session as `/anonymous/N`. Its highlighted
tray is previewed above fzf; a registered worktree without an in-session tray
has a dark preview. Opening the overlay leaves command mode so typing, arrows,
and Enter are routed to it. `Esc` continues to toggle command mode without
closing the overlay. Arrows, `Tab`, `Shift+Tab`, and `Enter` continue navigating
or selecting from the overlay while command mode is active. From command mode,
`Shift+W` closes the overlay and leaves command mode active.

Each tray owns its worktree-management overlay state. Switching trays hides the
previous tray's overlay without destroying it; switching back redraws the tray
and its overlay with the input and cursor preserved. Helper PTYs owned by
inactive overlays continue being drained.

## Persistent Registry

Persist one protobuf registry per bridge instance at:

```text
$XDG_STATE_HOME/my-opiniated-editor/instances/port-<port>/worktrees.pb
```

When `XDG_STATE_HOME` is unset, use:

```text
$HOME/.local/state/my-opiniated-editor/instances/port-<port>/worktrees.pb
```

The bridge passes the instance state directory explicitly to its C++ parent.
The standard runner requires a port argument and uses that port as the stable
instance identity, so a manual-testing bridge cannot modify another port's
registry.

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

- Add a parent-owned `fzf` path-picker overlay.
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

- A parent-owned fzf selector switches among manually registered worktrees.
- Canceling does not switch trays or leak Escape to the content PTY.
- The full Bazel test suite passes.
- One commit is ready for manual review.

## Milestone 9: Create A Worktree

Deliverables:

- Fold existing-worktree selection into `Shift+W` as its first and default
  Worktrees mode.
- Add mutually exclusive Add worktree and Add repository modes.
- Use `Tab` or `Shift+Tab` to cycle modes directly and reset mode-local state.
- Reuse the fzf path picker to show and select all registered repositories in
  Add worktree mode.
- Ask for a branch name and derive a worktree path below the selected
  repository, replacing branch-name slashes with hyphens.
- Start provisioning immediately after the branch is submitted; do not ask for
  path confirmation.
- If the path already contains a linked worktree for the selected repository
  and branch, register it using filesystem metadata without running Git.
- If the path does not exist, first attempt to fetch that branch from origin.
  Check out an existing local branch when present, create a tracking branch
  when only the fetched remote branch exists, or create a new branch from the
  repository default when neither exists.
- Atomically add the new worktree to the registry.
- Open its worktree tray immediately.
- Include used anonymous trays in Worktrees and preview the focused in-session
  tray above fzf.

Repository mode retains repository registration and bare-root creation. A
single flow asks for either worktree information or repository information,
never both. Additional worktrees for a registered repository do not repeat
repository registration.

Creating a genuinely new branch is equivalent to:

```text
git worktree add -b <branch> <path> <default-branch>
```

The editor must not guess between `main` and `master`. Missing or ambiguous
default-branch metadata is an actionable error.

If Git succeeds but persistence fails, keep the Git worktree, report the
partial success, and allow it to be adopted during a later management action.

Tests:

- Default-branch resolution.
- Existing local branches, fetched remote branches, and fetch failure fallback.
- Branch-name and path validation.
- Derived paths for branch names containing slashes.
- Existing matching worktrees are adopted without a Git process.
- Mismatched branches, duplicate branches, and invalid existing destinations.
- Git and persistence failures.
- Immediate switch into the new worktree tray.
- Anonymous tray candidates and focused tray previews.

Exit criteria:

- `Esc`, `Shift+W` handles worktree switching, repository addition, and
  worktree creation.
- Closing the overlay with `Shift+W` leaves command mode active.
- New worktrees always branch from the repository default branch.
- The full Bazel test suite passes.
- One commit is ready for manual review.

## Milestone 10: Clear Or Remove A Tray

Deliverables:

- Add command-mode-only `Shift+C` and `Shift+R` actions to the Worktrees fzf
  picker.
- Resolve both actions from the picker's highlighted structured `TrayId`, not
  from displayed path text.
- `Shift+C` and `Shift+R` show a `y/N` confirmation without leaving command
  mode. `N`, `Enter`, or `Esc` cancels without changing state.
- Confirming `Shift+C` destroys the highlighted tray's overlay, content PTY,
  terminal screen, and shell process without changing Git or the protobuf
  registry.
- Confirming removal of a worktree asks Git to force-remove the linked
  worktree, including stale metadata for a worktree missing from disk, then
  atomically removes the worktree from the protobuf registry and destroys any
  in-session tray.
- The worktree used as the parent process's startup directory is protected from
  removal. `Shift+R` reports an error immediately instead of opening the
  confirmation, and the removal boundary rejects the same path as a backstop.
- A worktree already absent from Git is silently unregistered. A Git failure
  that leaves the worktree registered does not alter the protobuf registry or
  destroy its tray.
- Confirming removal of an anonymous tray only destroys that tray.
- Keep tracked worktrees that are missing or otherwise unavailable visible in
  the picker with an `[unavailable]` suffix. Enter does not open them, but
  `Shift+R` can still purge stale Git metadata and unregister them.
- When the active content shell exits or the active tray is explicitly
  destroyed, switch to anonymous tray 1. Destroying anonymous tray 1 starts a
  fresh anonymous tray 1 in the configured user's home directory with the
  current terminal size.
- Destroying an inactive anonymous tray 1 leaves it absent. It is recreated
  only when the active tray is later destroyed and needs the fallback.
- Refresh the Worktrees picker after an inactive tray is cleared or removed;
  destroying the active tray closes its attached picker as part of destroying
  the tray.

Tests:

- Command routing is inactive outside command mode and outside the Worktrees
  picker.
- `Shift+C` and `Shift+R` confirmation and cancellation; successful purge,
  stale-worktree pruning, already-purged unregistration, and Git/persistence
  failures.
- `Shift+C` preserves Git and registry state, and command mode continues to
  route arrows, `Tab`, and `Shift+Tab` to the overlay.
- Destroying active and inactive anonymous and worktree trays terminates their
  content PTYs and selects the documented fallback.
- Exiting a content shell destroys its tray and selects or recreates anonymous
  tray 1.
- Browser assets and WebSocket integration cover the new shortcuts and shared
  parent status after an active tray is destroyed.

Exit criteria:

- Clearing a tray never changes the persistent worktree model.
- Removing a worktree leaves Git, the registry, and in-session trays in a
  consistent recoverable state.
- Anonymous tray 1 remains available after every destruction path.
- The full Bazel test suite passes.
- One commit is ready for manual review.

## Reconciliation Rules

When the registry or a selector is opened:

- Validate registered roots using Git.
- Compare persisted worktrees with `git worktree list --porcelain`.
- Keep missing persisted paths but mark them unavailable.
- Detect live Git worktrees not yet tracked by the editor.
- Never silently delete persisted repository or worktree entries.

Worktrees mode shows tracked worktrees and labels unavailable entries so they
can be removed explicitly. Repository removal and bulk reconciliation actions
are out of scope for these milestones.

## Architecture Boundary

```text
browser
  -> action message
bridge
  -> parent control command
workspace parent
  -> tray-owned overlay lifecycle, command-mode routing, and pane redraw
worktree workflow
  -> registry/live-Git candidate intersection and validation
path picker
  -> temporary fzf PTY, candidate display, filtering, and selection
worktree registry store
  -> protobuf loading and atomic persistence
tray manager
  -> running tray and content-PTY lifecycle
```

The worktree overlay implements the `Overlay` interaction contract and owns its
small form editor. It composes a reusable path picker for Worktrees mode and
the repository-selection step of Add worktree mode. The path picker owns a
temporary fzf PTY and terminal screen because fzf handles its own query text
and arrow input. Command mode remains above the worktree overlay, so entering
it never leaks Escape into either child surface.

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
