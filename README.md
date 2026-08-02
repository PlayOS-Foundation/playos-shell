# PlayOS Shell

> Controller-first Raylib shell. The persistent console UI that is always alive, even while a game runs.

**Dependency position:** `playos-shell` depends on `playos-platform-api` (public `libplayos` API) and uses a restricted `playos-runtime` control client for trusted launch requests.

## What This Repository Owns

- The persistent console UI application
- Controller-first navigation (D-pad, A/B/X/Y)
- Game library discovery and display
- User-facing launch, resume, quit, and crash flows
- Settings and status screens
- Initiating `LaunchGame` requests via control IPC
- Rendering with the Raylib PlayOS backend (`rcore_playos.c`)
- UI state preservation while a game runs

## Screens

| Screen | Trigger |
|---|---|
| Library | Default; shown on startup and after game exit |
| Game Detail | Select a game in the library |
| Launching | After LaunchGame is sent |
| Settings | Start button |
| System Update | From settings |

## Controller Navigation

| Button | Action |
|---|---|
| D-pad | Navigate focus |
| A | Confirm / select |
| B | Back / cancel |
| Start | Open settings |

## Building

```bash
cmake -S . -B build
cmake --build build
```

Requires: `libplayos` (from `playos-platform-api`), `raylib`

## Documentation

Full specification: [`playos-spec/playos-shell-spec.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/playos-shell-spec.md)
