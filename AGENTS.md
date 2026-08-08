# AGENTS.md — playos-shell

> **Implementation status:** 🔴 Pre-implementation — architecture and screen map defined in `playos-spec`. No source code yet (`CONTRIBUTING.md` only). This AGENTS.md describes the **target** structure.

This repository implements the **PlayOS shell** — a controller-first, fullscreen Raylib application that runs permanently as a Wayland client. It is the UI the user sees at boot and between games: the home screen, game library, settings, and system overlay trigger.

## Specification Reference

Before touching any file here, read:
- [`playos-spec/src/playos-shell-spec.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/playos-shell-spec.md) — all screens, navigation rules, launch flow, input handling
- [`playos-spec/src/playos-overlay-spec.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/playos-overlay-spec.md) — how the overlay interacts with the shell
- [`playos-spec/src/architecture.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/architecture.md) — shell's place in the system

## Shell Screen Map

```
HOME ──── LIBRARY ──── GAME_DETAIL
  │
  └──── SETTINGS
           ├── display
           ├── audio
           ├── power
           └── system (firmware update)
```

Navigation is **always controller-only** — no mouse, no keyboard (keyboard may be used in dev mode only).  
The `B` button always means "back". The `A` button means "select/confirm".

## Repository Layout

```
src/
├── main.c              ← Entry point, Raylib init, Wayland client setup, event loop
├── shell.h             ← Screen enum, navigation stack, global state struct
├── screen_home.c       ← Home screen render + input
├── screen_library.c    ← Game library grid render + input
├── screen_game_detail.c← Game detail / launch confirm screen
├── screen_settings.c   ← Settings screens (tabbed)
├── launcher.c          ← Sends launch_game over playos_session_manager protocol
├── input.c             ← Gamepad polling via evdev backend (trusted, includes SYSTEM/QUICK_MENU)
├── audio_ui.c          ← UI sound effects (short clips via libplayos audio API)
└── assets/             ← Fonts, icons, audio clips (committed as binary blobs)

include/
└── shell.h

CMakeLists.txt
```

## Key Invariants — Do Not Break

- **The shell must always be running** — it is supervised by `playos-init`. Do not call `exit()` except on unrecoverable init failure.
- **Navigation stack discipline**: every screen push must have a corresponding pop path. There must always be a way back to HOME.
- **Controller input only** for navigation. Do not read keyboard keycodes for navigation logic (allowed only in dev text-entry fields).
- **No direct IPC socket access** — use `libplayos` (from `playos-platform-api`) for system, storage, lifecycle, and logging. **Exception for input:** the shell is trusted and needs SYSTEM/QUICK_MENU buttons that `libplayos` input API strips. The shell reads controller input directly through the evdev backend (see Sprint 5 review decision). Long term, this will move to a privileged compositor protocol.
- **Launch flow**: shell sends `playos_session_manager.launch_game` → compositor handles the rest → shell receives a lifecycle event when game exits. The shell must not assume the game has started until the compositor confirms.
- **60 fps target**: all screen renders must complete within 16ms. No blocking I/O on the render thread.

## Raylib Backend

PlayOS uses a **custom Raylib backend** (`rcore_playos.c`) instead of the default GLFW backend. This backend:
- Creates a Wayland `wl_surface` bound via the private `playos_game_surface` protocol (shell gets a special trusted surface type).
- Submits frames via `eglSwapBuffers` on the Wayland EGL surface.
- Forwards controller events from the evdev backend into Raylib's input state. (Note: does not use `playos_input_get_controller_state()` — see input exception above.)

Do not use `InitWindow()` with the default Raylib GLFW path — use `PlayOSInitDisplay()` which sets up the custom backend.

## Code Conventions

- C99. Link against Raylib (custom backend), libplayos, libwayland-client, libEGL.
- All screen modules expose exactly three functions: `screen_NAME_enter()`, `screen_NAME_update()`, `screen_NAME_draw()`.
- Global shell state is in a single `struct playos_shell` — no global mutable variables outside it.
- Asset loading happens at startup (`screen_NAME_enter`), not per-frame.
- UI dimensions are specified as fractions of `GetScreenWidth()`/`GetScreenHeight()` — never hardcoded pixel values.

## Build Commands

```sh
cmake -B build
cmake --build build
# Dev: run inside a nested Wayland session (see playos-spec/src/dev-environment.md)
WAYLAND_DISPLAY=wayland-1 ./build/playos-shell
```

## What NOT to Do

- Do not read from the filesystem for game metadata at runtime — metadata is cached by `playos-init` at boot and served via IPC.
- Do not launch games by exec()-ing directly — always go through `launcher.c` which uses the Wayland protocol.
- Do not draw the overlay UI here — that is `playos-overlay`'s job.
- Do not add a web browser, terminal emulator, or any non-gaming UI surface.
- Do not add mouse/touchscreen handling for production — controller only.
