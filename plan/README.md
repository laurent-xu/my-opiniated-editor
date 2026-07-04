# Dev Workspace Planning

This directory contains the planning documents for a browser-accessed,
keyboard-first, AI-agent-native development workspace.

## Current Recommendation

Build an owned C++ parent workspace/editor process and serve that process
through one primary browser PTY. The browser bridge should be thin: xterm.js for
the terminal surface, WebSocket for bytes/control, and browser Clipboard API for
client clipboard writes.

The C++ parent process owns tiling, editor behavior, shell panes, agent panes,
LSP/build state, plan review, and command routing. Shells and coding-agent CLIs
can still run in PTYs, but those are child sessions managed by the parent C++
app, not independent browser-bridge sessions.

The important distinction:

- Own the productivity-critical model: commands, keymaps, workspace graph,
  editor state, agent sessions, plan feedback, LSP/build diagnostics, and
  self-hosting build integration.
- Rent the plumbing: browser terminal rendering, WebSocket, one parent PTY
  bridge, browser clipboard APIs, reverse proxy, and auth libraries.

Recommended first stack:

- Parent workspace/editor app: C++23.
- Bridge: Rust first, with Node as a throwaway spike option and C++ later if a
  single-language system becomes worth it.
- Browser UI: thin TypeScript host around xterm.js plus clipboard/auth/status
  controls.
- Terminal surface: one primary xterm.js instance attached to the parent C++
  app's PTY.
- Editor: owned C++ editor inside the parent app. CodeMirror 6 is deferred to a
  later optional browser-native view adapter if it becomes useful.
- C++ tooling: clangd, clang-format, clang-tidy, Bazel-generated
  `compile_commands.json`.
- Python tooling: Ruff plus basedpyright or pyright.
- Web tooling: TypeScript language server plus project-local formatter/linter.
- Build system: Bazel for this project, with enough integration that the editor
  can build and test itself.
- Environment: NixOS is the current development environment, but not a core
  editor feature yet.

## Document Map

- [Start Here](START_HERE.md)
- [Product Principles](product/vision-and-principles.md)
- [Keyboard UX](product/keyboard-first-ux.md)
- [Agent Feedback Workflows](product/agent-feedback-workflows.md)
- [Agent Coding Workflow](product/agent-coding-workflow.md)
- [System Architecture](architecture/system-architecture.md)
- [Bridge Protocol](architecture/browser-bridge-protocol.md)
- [Git Workspace Model](architecture/git-workspace-model.md)
- [Language and Build Tooling](architecture/language-build-tooling.md)
- [Project Build Runtime](architecture/project-build-runtime.md)
- [MVP Roadmap](implementation/mvp-roadmap.md)
- [Bazel Setup Plan](implementation/bazel-setup-plan.md)
- [Risks and Open Questions](implementation/risks-and-open-questions.md)
- [Testing Plan](testing/README.md)
- [Decision 0001: Own the Parent C++ Workspace Process](decisions/0001-own-parent-cpp-workspace.md)
- [Decision 0002: Bridge Technology](decisions/0002-bridge-technology.md)
- [Decision 0003: LSP, Bazel, and Project Environment](decisions/0003-lsp-bazel-project-environment.md)
- [References](references.md)

## Review Style

Treat each file as a proposed direction, not a commitment. The decision records
are intentionally explicit so that rejected ideas stay visible and can be
revisited without losing the reasoning.
