# Agent Feedback Workflows

## Agent Sessions As Objects

Each agent session should have durable metadata:

- Session ID.
- Agent kind: Codex, Claude Code, shell, or custom.
- Workspace root.
- Primary or sub-agent role.
- Git branch and worktree.
- Start commit.
- Selected diff base at time of review.
- Spawn command.
- Environment name.
- Current status.
- Objective.
- Transcript.
- Extracted plans.
- Files touched.
- Diffs produced.
- Tests/builds run.
- User feedback events.

Do not treat agent sessions as anonymous terminal tabs. A terminal pane can be
one view of a session, but the session itself should be navigable and queryable.

## Wrapping Agent CLIs

Initial implementation can spawn agent CLIs in parent-owned child PTYs for
compatibility:

```text
C++ parent workspace app -> internal child PTY manager -> codex or claude CLI
```

The browser bridge should not expose a separate WebSocket PTY for each agent in
the first design. It serves the parent workspace PTY; the parent app owns agent
processes, layout, transcript capture, and feedback routing.

Multiple CLI-agent orchestration is an early self-hosting requirement. Before
the plan review UI is sophisticated, the parent app should still be able to run
several child agent CLIs, keep their transcripts separate, switch focus by
keyboard, and send feedback to the selected session.

Agent sessions should be easy to find by shelling out to `fzf` and should be
groupable by status: working, waiting-for-feedback, done, failed, stopped, and
archived.

The workspace should also capture sidecar state:

- Raw transcript log.
- Parsed plan blocks.
- Detected file references.
- Detected commands.
- Detected test results.
- User feedback events.

Terminal scraping will be imperfect. Treat it as a compatibility layer, not the
long-term protocol. If an agent exposes a structured API later, adapt to that.

## Plan Review Model

When an agent proposes a plan, parse it into stable sections:

```text
plan
  section: objective
  section: assumptions
  section: steps
    item: inspect workspace
    item: implement bridge
    item: verify clipboard
  section: risks
```

Each section or item should have:

- Stable ID.
- Source session and transcript range.
- Status: unread, accepted, rejected, needs-change, superseded.
- User comments.
- Linked files, diagnostics, diffs, symbols, or targets.
- Feedback sent back to agent.

Each newly detected plan should be stored as a new proposal version. The user
should be able to diff the latest proposal against the previous proposal from
the same agent and annotate only the changed sections.

## Agent Views

Each agent has two primary views:

- Full UI view: the agent CLI as a terminal view for raw interaction.
- Plan-only view: extracted plan sections with annotations, statuses, comments,
  plan diffs, and targeted feedback commands.

Switching between views should not spawn a new process. Both views point to the
same durable agent session.

## Feedback Commands

Useful commands:

- Accept current plan section.
- Reject current plan section.
- Mark section as needing changes.
- Draft feedback for selected section.
- Send selected feedback to agent.
- Attach current diagnostic to feedback.
- Attach current diff hunk to feedback.
- Ask agent to revise only selected sections.
- Ask agent to continue from accepted sections.
- Copy selected plan context to clipboard.
- Compare plans from two sessions.
- Diff latest plan against previous proposal.
- Open selected agent in full UI view.
- Open selected agent in plan-only view.
- Spawn sub-agent for current file.

Feedback should be targeted. Avoid dumping an entire transcript back into an
agent when a section-level comment is enough.

## Multi-Agent Workflow

Default to one worktree per non-trivial agent session:

```text
repo
  worktree/main
  worktree/agent-codex-123
  worktree/agent-claude-456
```

The workspace should make it cheap to:

- Compare two agents' plans.
- Compare diffs from two sessions.
- Route the same feedback to multiple sessions.
- Promote one session's diff.
- Kill, archive, or resume stale sessions.
- Jump between sessions by shelling out to `fzf`.
- Show sessions by status.
- Show files most recently changed by each agent.

Default to one primary agent per git worktree. Allow manual sub-agents scoped to
the currently open file, selection, diagnostic, target, or plan section.

## Safety Gates

The app should surface high-risk actions before they happen:

- Destructive shell commands.
- Git history rewrites.
- File deletion.
- Dependency updates.
- Large mechanical rewrites.
- Commands outside the workspace.

The safety gate belongs in the workspace command model, not only in the agent
prompt. Agents will make mistakes; the workspace should keep state legible.
