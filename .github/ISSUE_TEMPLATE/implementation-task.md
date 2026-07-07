---
name: Implementation Task
about: A scoped task an AI agent or contributor can implement.
title: "Task: "
labels: [implementation, agent-ready]
assignees: []
---

## Goal

<!-- One sentence: what should exist after this task is complete. -->

## Source of truth (read first)

- `AGENTS.md`
- `.github/copilot-instructions.md`
- Relevant `playos-spec` chapters:
  <!-- e.g. book/src/09-shell-and-ux/* -->

## Required output

<!-- Files to create or modify, and the UX/behavior expected. -->

## Acceptance criteria

- [ ] Builds: `cmake -B build && cmake --build build`
- [ ] Controller-first: reachable with a gamepad
- [ ] Platform services go through the PlayOS Platform API (Raylib = rendering only)
- [ ] Games are launched via `PlayOS::Runtime`

## Constraints

- Follow this repository's `AGENTS.md`.
- Keep the change scoped to this task.
