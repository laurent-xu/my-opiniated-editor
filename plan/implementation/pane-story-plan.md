# Pane Story Plan, Version 2

## Milestone

Phase 2, skeleton workspace: make parent-owned terminal panes useful end to end
through the browser while keeping the browser bridge stateless about layout.

This version records the implemented interaction contract. It replaces the
earlier binary-tree and layout-history ideas.

## Deliverables

### Ownership And Runtime

- The C++ parent owns every pane, child PTY, terminal screen, layout mutation,
  focus decision, selection, move session, and tray-local interaction state.
- Each terminal pane owns an independent child PTY and screen. All pane PTYs
  remain in the poll/drain set; only the focused pane receives keyboard input.
- The parent sends each child PTY's raw bytes through a pane-tagged view stream.
  The browser mirrors the parent-owned tree with one persistent xterm.js
  instance per leaf. The outer PTY remains the overlay and compatibility
  surface; the bridge does not own or interpret the layout.
- A tray switch preserves that tray's layout, focused pane, maximized state,
  selection, and incomplete move session. A browser reconnect observes the
  same parent-owned state.

### Normalized N-Ary Layout

- A leaf is a pane. A split node is an ordered list of two or more children,
  one axis, and one integer percentage per child.
- Repeated splits on one axis share an N-ary level. Three side-by-side panes
  are `A | B | C`, not a chain of binary nodes.
- A different axis creates exactly one nested level. `A | (B-C)` is a
  side-by-side root whose right child is an above-below group.
- No split may directly contain another split with the same axis. Split,
  close, move, swap, and rotate normalize the tree back to this invariant.
- Percentages are integers from 0 through 100 and total exactly 100 at every
  split. Proportional changes use deterministic largest-remainder rounding;
  earlier siblings win exact ties.
- A zero-percent child remains alive even though it is not rendered.

### Focus, Selection, And Sizing

- Directional focus uses rendered geometry rather than creation order.
- Selection starts at the focused leaf and may extend only across contiguous
  children of one direct parent.
- Selection treats a nested split as one complete node at its parent's level.
  In `A | (B-C)`, valid selections include `(B-C)`, `B-C` inside that group,
  and `A | (B-C)`. Selecting `A` and `B` without `C` is impossible.
- Promote selects the current selection's parent as one node. Descend selects
  the first direct child of a single selected split.
- Grow and shrink change the selected range's combined share in five-point
  increments and preserve its internal proportions. The complementary share
  is redistributed among unselected siblings.
- Equalize distributes an explicit selection's existing combined share evenly
  among only those selected siblings; unselected sibling percentages do not
  change. When the explicit selection is one split node, equalize instead
  distributes that node's share evenly among its direct children; selecting
  one leaf is a no-op. Without a selection, equalize applies to the focused
  pane's sibling level.
- Maximize is a reversible view state, not a layout mutation.
- The focused pane stays at full brightness during normal use and other panes
  are dimmed. During selection, every leaf below the selected node or range
  stays bright and all other panes are dimmed. Faint nested-group outlines and
  a left-to-right or top-to-bottom badge identify the active selection level.

### Moving And Swapping

Move is a visible two-stage transaction. The live tree does not change until
confirmation.

1. `Shift+M` captures the complete selected range, or the focused leaf when
   there is no selection, and enters target selection.
2. `Shift+Arrow` chooses a complete target by geometry. `Shift+[` promotes the
   target to its parent; `Shift+]` descends into a selected target group.
3. `Shift+Enter` locks the target.
4. `Shift+Arrow` now means place the source immediately left, right, above, or
   below that target. The parent renders the derived layout as a preview.
5. A second `Shift+Enter` commits the preview. `Shift+M` cancels at any stage.

For example, in `A | (B-C)`, select `B-C`, start move, choose `A`, lock the
target, and choose left. The preview is `(B-C) | A`; confirmation reparents the
complete group into the root side-by-side split.

A same-axis placement inserts or reorders at the existing N-ary level. A
different-axis placement wraps the target in the one necessary nested split.
Normalization removes any redundant same-axis level afterward.

`Shift+S` during target selection changes a single-node move into swap. The
target is the complete highlighted tree node. Swap exchanges source and target
while their destination slots retain their percentages; normalization may
flatten a moved group when its new parent has the same axis. Swap previews can
be confirmed immediately with `Shift+Enter`.

Multi-node ranges cannot swap. Source and target cannot be the same node or
have an ancestor/descendant relationship. Cross-tray moves and multiple
disjoint source ranges are not supported.

### Command-Mode Keymap

All pane bindings require parent command mode. Shifted bindings are used so
unshifted keys remain available for future editor and clipboard commands.

| Binding | Normal or selection context | Move context |
| --- | --- | --- |
| `Shift+Arrow` | Focus, or extend the same-level range | Choose target, then choose placement side |
| `Shift+V` | Add a pane to the right; vertical divider | - |
| `Shift+H` | Add a pane below; horizontal divider | - |
| `Shift+S` | Start or clear selection | Toggle move/swap while choosing a target |
| `Shift+[` / `Shift+]` | Promote / descend selection | Promote / descend target |
| `Shift++` / `Shift+-` | Grow / shrink by five percentage points | - |
| `Shift+E` | Equalize sibling level | - |
| `Shift+M` | Start move | Cancel move |
| `Shift+Enter` | - | Lock target or confirm preview |
| `Shift+R` | Rotate selected hierarchy, or the focused level | - |
| `Shift+Z` | Maximize / restore focused pane | - |
| `Shift+X` | Close focused pane | - |

`Shift+R` rotates every split axis inside the selected hierarchy. A proper
subset selection is wrapped on the opposite axis while keeping its combined
share; only percentages inside that selected range are renormalized, and
unselected sibling percentages do not change. If the rotated selection then
matches its parent axis, it is flattened, so nested leaves may become siblings
of a top-level pane. The logical selection follows that normalization, and
rotating it a second time restores the original hierarchy and percentages.

The browser status line publishes selection count and the active interaction
stage, with contextual hints for normal commands, selection, move target, move
side, swap target, and the worktree overlay.

## UX Precedents

- [tmux](https://man7.org/linux/man-pages/man1/tmux.1.html) separates
  directional focus, resize, marked source, explicit target, swap, and zoom.
- [kitty](https://sw.kovidgoyal.net/kitty/actions/) provides directional focus
  and movement, interactive resize modes with instructions, and visual target
  selection for swap.
- [Terminator](https://gnome-terminator.readthedocs.io/en/latest/gettingstarted.html)
  exposes split, sibling-level rebalance, directional resize, rotation, and
  maximize; it also permits a terminal to shrink completely out of view.
- [iTerm2](https://iterm2.com/documentation/2.1/documentation-preferences.html)
  makes pane movement explicitly staged by marking the source visually before
  choosing a destination.

The selected UX combines visible source/target staging with a derived preview.
It does not copy tmux's implicit marked-pane target or iTerm2's mouse-only
destination step because complete nested nodes need an unambiguous keyboard
path.

## Tests And Exit Criteria

Unit coverage must prove:

- Same-axis splits normalize into one N-ary level and differing axes nest once.
- Every percentage vector is integer-valued and totals 100 after all mutations.
- Same-level selection accepts complete contiguous nodes and rejects mixed
  levels such as `A` plus `B` without `C`.
- Group resize, equalize, rotate, move, and swap preserve the
  normalization invariants.
- Move preview leaves the live tree unchanged until confirmation and rejects
  overlapping source/target ancestry.
- Pane commands encode, parse, and dispatch as typed parent actions.

Integration coverage must prove:

- Independent pane PTYs publish distinct raw streams and only focus receives
  input.
- Browser pane viewports resize their corresponding PTYs exactly; maximize
  restores their split sizes.
- Selection and move overlays render, tray switching preserves interaction
  state, and close/child exit leaves surviving panes usable.
- Browser command-mode pane actions reach the parent layout.
- Parent pane mode and selection state are replayed after browser reconnect.

Default confidence command:

```text
bazel --batch test //...
```

The story exits when that command passes and the worktree is clean after the
small implementation commits.

## Intentionally Out Of Scope

- Undo or redo for layout mutations.
- Named layouts or persistence across a parent-process restart.
- Mouse drag/drop or resize handles.
- Cross-tray moves, disjoint multi-selection, or swapping multi-node ranges.
- User-remappable bindings before a general command registry needs them.
- Editor and clipboard commands on the reserved unshifted key space.
- Pinning, scratch-pane types, and non-terminal pane objects.
