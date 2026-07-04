# Risks and Open Questions

## Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Browser keyboard shortcuts conflict with OS/browser shortcuts | Breaks keyboard-only flow | Keep keybindings configurable; add extension later for deeper capture |
| iPhone browser limitations | Mobile editing may disappoint | Treat mobile as review/steering first, not the main editing surface |
| Terminal scraping of agent CLIs is fragile | Plan parsing can break | Store raw transcripts; make parsing best-effort; adapt structured APIs later |
| LSP client implementation is non-trivial | Slow editor progress | Start with a small subset: initialize, open/change/save, diagnostics, definition |
| Bazel compile database becomes stale | clangd gives wrong answers | Track BUILD/MODULE/.bazelrc changes and expose refresh commands |
| Project environment changes invalidate processes | LSP/build behavior becomes confusing | Start with inherited environment; add explicit environment tracking only when needed |
| Parent PTY UI limits browser-native affordances | Some rich inspection views are harder early | Keep a small control sideband and add browser-native inspectors only when needed |
| Terminal rendering becomes the only source of structured state | Plan/diagnostic feedback becomes fragile | Store plans, diagnostics, sessions, and comments in the parent app model, not only in screen text |
| Multiple agents edit same repo | Conflicts and lost work | Default to per-agent git worktrees for non-trivial sessions |
| Remote exposure is high risk | Browser workspace can control shell and clipboard | Use private network, HTTPS, strong auth, and auditable clipboard commands |

## Open Questions

- Should the first editor source of truth be the C++ parent app or a browser
  editor component?
  Recommendation: the C++ parent app. CodeMirror can be revisited later as an
  optional browser-native view for specific workflows.
- Should the first bridge be Rust or Node?
  Recommendation: Rust unless the first PTY spike gets stuck. Node is fastest
  for throwaway proof, but less attractive as a long-term native service.
- Should every agent session get a worktree?
  Recommendation: yes for sessions expected to edit files; no for read-only
  planning or quick shell-like questions.
- How much plan parsing should be heuristic?
  Recommendation: enough to extract headings, checklists, and numbered lists.
  Do not overfit to one agent's terminal format.
- Should Bazel be the only test runner?
  Recommendation: prefer Bazel where targets exist, but support direct pytest
  and frontend test commands as task adapters.
- Is a browser extension mandatory?
  Recommendation: no for MVP. It becomes likely once shortcut capture and
  clipboard permission friction are proven.
