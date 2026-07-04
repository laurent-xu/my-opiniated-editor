# Agent Coding Workflow

## End-State Workflow

The editor should make coding with multiple agents feel like managing structured
work, not like babysitting terminal tabs.

Core workflow:

- Run multiple coding agents in parallel.
- Jump between agents through a real `fzf` subprocess.
- Group agents by status: working, waiting-for-feedback, done, failed,
  archived.
- Open an agent in full UI mode when the raw CLI matters.
- Open an agent in plan-only mode when reviewing and annotating proposed work.
- Diff a new plan proposal against the previous proposal from the same agent.
- Track files changed by each agent in each git workspace.
- Select a base commit interactively for diffs.
- Usually run one primary agent per git worktree.
- Spawn a focused sub-agent on the currently open file with a shortcut.

## Agent Dashboard

The parent app should have an agent dashboard that can be opened from the
keyboard. It should support:

- Filter by status.
- Filter by workspace.
- Filter by branch/worktree.
- Search by objective, file touched, plan heading, or session ID.
- Jump to selected session.
- Open selected session in full UI mode.
- Open selected session in plan-only mode.
- Kill/archive/resume selected session.
- Copy session summary.

The finder should shell out to `fzf` early. The parent app should pass a
structured list of candidates to `fzf` and parse only the selected stable ID
from stdout. Do not make terminal screen scraping part of the model.

## Agent Statuses

Initial statuses:

- `starting`: process created but not ready.
- `working`: agent is actively producing output or running commands.
- `waiting-for-feedback`: agent produced a plan/question or paused for input.
- `done`: agent says work is complete or process exited successfully.
- `failed`: process exited unexpectedly or reported unrecoverable failure.
- `stopped`: user stopped the session.
- `archived`: hidden from default active views.

Status should be derived from process state plus heuristics over agent output.
It should be manually overrideable because CLI agents will not always report
state cleanly.

## Agent Views

Each agent session should have two primary views.

Full UI mode:

- Shows the agent CLI exactly enough to interact with it.
- Preserves terminal behavior for prompts, command output, and scrolling.
- Allows raw input when needed.
- Still captures transcript, file updates, and plans in sidecar state.

Plan-only mode:

- Shows extracted plan proposals as structured sections.
- Allows annotations on specific headings, bullets, or checklist items.
- Allows accept/reject/needs-change status per section.
- Can send targeted feedback back to the agent.
- Can show links to touched files, diffs, diagnostics, tests, and build output.

Switching views should not create a new agent process. Both views are projections
of the same agent session object.

## Plan Diffing

Agents often revise plans. The editor should keep every detected plan proposal
as a versioned object:

```text
agent session
  plan proposal 1
  plan proposal 2
  plan proposal 3
```

Useful commands:

- Show latest plan.
- Show previous plan.
- Diff latest plan against previous.
- Diff selected plan versions.
- Copy plan diff.
- Annotate changed section.
- Ask agent to explain only the changed sections.

Plan diffing should work at section/list-item level when possible, with a text
diff fallback when parsing fails.

## Workspace File Activity

For each git workspace/worktree, the editor should show files most recently
updated by the agent.

Useful views:

- Latest updated files.
- Files grouped by agent session.
- Files grouped by status: modified, added, deleted, renamed.
- Files grouped by Bazel target when known.
- Files with diagnostics after agent changes.
- Files changed since selected base commit.

This is important when an agent says it is done. The review path should start
from what changed, not from reading the whole transcript.

## Interactive Diff Base

For each git workspace, the editor should let the user pick a base commit for
diffs.

Base commit picker should support:

- Current branch merge-base with main.
- Previous commit.
- Recent commits.
- Tags.
- Manually typed revision.
- Agent session start commit.

The chosen base commit should affect:

- Workspace diff view.
- Latest updated files view.
- Agent changed-files summary.
- Copy diff commands.
- Review prompts sent to agents.

The base is workspace-specific and should be visible in status surfaces.

## Primary Agent And Sub-Agents

Default workflow:

- One primary agent per git workspace.
- The primary agent owns the main objective for that workspace.
- Sub-agents are manually spawned for focused work.

Focused sub-agent examples:

- Review the currently open file.
- Explain a selected function.
- Propose tests for the current file.
- Investigate a diagnostic in the current file.
- Compare current file against selected diff base.

Sub-agent creation should be a shortcut-driven command that captures the current
file, selection, diagnostics, active Bazel target, and workspace base commit.

## Essential Commands

- `agent.find_with_fzf`
- `agent.find_by_status`
- `agent.open_full_ui`
- `agent.open_plan`
- `agent.toggle_full_ui_plan`
- `agent.spawn_primary_for_workspace`
- `agent.spawn_subagent_for_current_file`
- `agent.send_feedback_to_selected_section`
- `agent.diff_latest_plan`
- `agent.diff_selected_plans`
- `workspace.latest_updated_files`
- `workspace.select_diff_base`
- `workspace.show_diff_from_base`
- `workspace.show_agent_changed_files`
