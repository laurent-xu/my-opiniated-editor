# Keyboard-First UX

## Operating Model

The parent workspace app has one active focus context at all times:

- Workspace context: panes, tabs, sessions, workspaces.
- Editor context: file edits, symbols, diagnostics, selections.
- Terminal context: parent-owned shell or agent child PTY.
- Plan review context: plan sections, comments, approvals, feedback.
- Fuzzy finder context: commands and object selection.

Every command has a stable name and may have zero or more keybindings. The
parent app's keybinding layer should call command names, not pane-specific
methods.

## Command Palette

The command palette is the root escape hatch. It should fuzzy-search:

- Commands.
- Open panes and tabs.
- Files.
- Symbols.
- Bazel targets.
- Tests.
- Diagnostics.
- Workspaces.
- Agent sessions.
- Agent sessions by status.
- Agent plans.
- Plan sections.
- Git commits for diff base selection.
- Recently updated files.
- Recent clipboard payloads.

The palette should support object pipelines:

```text
target: //server:unit_tests -> command: test.focused-target
diagnostic: missing include -> command: open.related-location
plan-section: "MVP bridge" -> command: agent.feedback-on-section
agent: codex-123 working -> command: agent.open_plan
commit: abc1234 -> command: workspace.set_diff_base
file: src/core/session.cc -> command: copy.path
```

## Keymap Direction

Use a small global keymap and a larger command palette rather than trying to
bind everything early.

Suggested global groups:

- `Ctrl-Space`: command palette.
- `Ctrl-P`: file finder.
- `Ctrl-B`: Bazel target finder.
- `Ctrl-D`: diagnostics finder.
- `Ctrl-A`: agent/session finder.
- `Ctrl-W`: workspace finder.
- `Ctrl-G`: plan section finder.
- `Ctrl-Shift-A`: agent status finder, if the terminal/browser stack can
  capture it reliably.
- `Ctrl-Shift-D`: workspace diff base picker, if available.
- `Ctrl-L`: focus location/status command, if browser conflicts are handled.
- `Esc`: return to normal workspace focus.

Agent, file, workspace, and commit pickers should shell out to `fzf` early,
with stable IDs passed through selection output.

Browser-reserved shortcuts will vary by OS and browser. The app should detect
unusable bindings and let the user remap them. A later browser extension can
capture more shortcuts and handle persistent clipboard writes.

## Tiling

The current terminal-pane interaction contract is specified in
[Pane Story Plan, Version 2](../implementation/pane-story-plan.md). Its core
rules are:

- Use a normalized N-ary layout tree, not a binary split tree.
- Store integer sibling percentages that always total 100.
- Select only contiguous complete nodes at one direct-parent level.
- Move complete nodes through explicit source, target, preview, and confirm
  stages; swap uses one complete source and target.
- Use Shift-first command-mode bindings so unshifted keys remain available for
  editor and clipboard commands.
- Support focus, split, close, group resize, equalize, rotate, maximize, move,
  and swap.
- Do not provide undo or redo for layout mutations.

Named layouts, pinning, scratch-pane types, and parent-restart persistence are
future stories rather than part of this pane milestone.

Panes should contain objects, not just components:

- File editor.
- Terminal.
- Agent session.
- Agent plan-only review.
- Plan review.
- Diff.
- Latest updated files.
- Diagnostics.
- Build/test run.
- Search results.
- Workspace overview.

## Clipboard

Clipboard writes are commands:

- Copy selection.
- Copy file path.
- Copy repo-relative path and line.
- Copy current symbol.
- Copy diagnostic with context.
- Copy diff hunk.
- Copy plan section.
- Copy agent feedback draft.
- Copy terminal selection.
- Copy generated prompt/context bundle.

The server requests a clipboard write; the browser performs it. The server
never assumes access to the client clipboard.

## Mobile Caveat

iPhone access should work for review, agent steering, and emergency edits. It
should not define the power-user keymap. Hardware-keyboard iPad/iPhone support
can be improved later, but the desktop browser with a real keyboard is the
primary design target.
