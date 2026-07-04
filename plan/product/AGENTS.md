# Agent Instructions For Product Docs

These instructions apply under `plan/product/`.

## Product Bias

- Design for keyboard-only daily programming.
- Treat agent sessions, plans, diffs, git workspaces, and updated files as
  first-class objects.
- Keep `vim` as a short-lived compatibility bridge only; the built-in editor
  should replace it quickly.
- Favor workflows that make parallel agents easy to supervise and compare.

## Agent Workflow Requirements

Product docs should preserve these end-state capabilities:

- Fuzzy jump between multiple agent sessions.
- Group agents by status.
- Full UI view for raw agent CLI interaction.
- Plan-only view for annotation and targeted feedback.
- Diff latest and previous plan proposals.
- Show latest files updated by agents.
- Select workspace diff base interactively.
- Usually one primary agent per git worktree.
- Shortcut-spawned sub-agents for the current file or selection.

## Minimal Implementation Rules

- Avoid UI concepts that cannot be driven from the keyboard.
- Avoid generic dashboards without a concrete command path.
- Prefer a small command set with strong fuzzy finding over many fixed
  shortcuts.
