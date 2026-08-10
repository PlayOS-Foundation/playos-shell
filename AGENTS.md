# AGENTS.md — playos-shell

> **Implementation status:** 🟡 Partial implementation — EGL/GLES2 shell renders and navigates with frame-callback vsync. Hybrid input: Platform API for standard controls (always polled, independent fd), raw evdev for reserved SYSTEM/QUICK_MENU buttons (auto-retry on device discovery failure). Manifest JSON parsing added for game names/versions/descriptions. Lifecycle events handled (TERMINATE, SUSPEND, RESUME). Per-call IPC pattern for game launch. Stub manifests in refdistro overlay. Raylib integration deferred; direct EGL/GLES2 used instead.

This repository implements the **PlayOS shell** — a controller-first, fullscreen EGL/GLES2 application that runs permanently as a Wayland client. It is the UI the user sees at boot and between games: the home screen, game library, settings, and system overlay trigger.

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
├── main.c              ← Entry point, EGL/GLES2 init, Wayland client setup, frame-callback vsync loop
├── input.c             ← Hybrid input: Platform API for standard buttons/axes, raw evdev for SYSTEM/QUICK_MENU
├── screen_home.c       ← Home screen render + input (stub)
├── screen_library.c    ← Game library grid render + input (stub)
├── screen_game_detail.c← Game detail / launch confirm screen (stub)
├── screen_settings.c   ← Settings screens / tabbed (stub)
└── render_util.c       ← GLES2 text+rect helpers (no external font/texture lib)

include/
└── shell.h             ← Screen enum, navigation stack, global state struct

CMakeLists.txt
```

## Key Invariants — Do Not Break

- **The shell must always be running** — it is supervised by `playos-init`. Do not call `exit()` except on unrecoverable init failure.
- **Navigation stack discipline**: every screen push must have a corresponding pop path. There must always be a way back to HOME.
- **Controller input only** for navigation. Do not read keyboard keycodes for navigation logic (allowed only in dev text-entry fields).
- **No direct IPC socket access** — use `libplayos` (from `playos-platform-api`) for system, storage, lifecycle, and logging. **Exception for input:** the shell is trusted and needs SYSTEM/QUICK_MENU buttons that `libplayos` input API strips. The shell reads controller input directly through the evdev backend (see Sprint 5 review decision). Long term, this will move to a privileged compositor protocol.
- **Launch flow**: shell sends `playos_session_manager.launch_game` → compositor handles the rest → shell receives a lifecycle event when game exits. The shell must not assume the game has started until the compositor confirms.
- **60 fps target**: all screen renders must complete within 16ms. No blocking I/O on the render thread.

## Rendering

The shell currently uses **raw EGL/GLES2** (not Raylib). EGL surfaces are created directly via `wl_egl_window_create` + `eglCreateWindowSurface`. Rendering is done with GLES2 draw calls in `render_util.c` (rectangles, text via a built-in bitmap font).

Raylib integration is deferred to a future sprint. When integrated, it will use a custom backend (`rcore_playos.c`) instead of the default GLFW backend:
- Creates a Wayland `wl_surface` bound via the private `playos_game_surface` protocol.
- Submits frames via `eglSwapBuffers` on the Wayland EGL surface.
- Forwards controller events into Raylib's input state.

## Frame Callback Vsync (Sprint 5)

The main loop uses `wl_surface_frame` + `wl_display_dispatch` for presentation pacing:
1. Request a frame callback via `wl_surface_frame(surface)`.
2. Commit the surface to trigger callback delivery.
3. Block in `wl_display_dispatch()` until the compositor signals readiness (callback fires, sets `frame_pending = false`).
4. Render → `eglSwapBuffers` → next callback requested.

This is the standard Wayland vsync pattern — no busy-waiting, no `usleep` heuristics.

## Input (Sprint 5 — Direct Evdev)

All controller input is read **directly from evdev** (`/dev/input/event*`) by `shell_input_poll()` in `src/input.c`. This is the Sprint 5 pragmatic approach (S5-T5 option 1): the shell opens its own fd, decodes all standard buttons (face buttons, d-pad via both ABS_HAT and BTN_DPAD_* forms, shoulders, stick clicks) and reserved buttons (SYSTEM/QUICK_MENU), and does not depend on the Platform API input path (which strips reserved buttons and uses a separate fd with different detection criteria).

**Device discovery:** `find_gamepad_device()` scans `/dev/input/event*`, prefers Xbox/ASUS/ROG Ally named devices, falls back to first viable gamepad. Auto-retries on each frame if the initial discovery failed (driver not yet loaded at startup).

Button mappings:
- `BTN_SOUTH`/`BTN_EAST`/`BTN_WEST`/`BTN_NORTH` → face buttons (A/B/X/Y)
- `BTN_DPAD_*` or `ABS_HAT0X/Y` → d-pad (both forms handled, mutually exclusive)
- `BTN_TL`/`BTN_TR` → L1/R1, `BTN_THUMBL`/`BTN_THUMBR` → L3/R3
- `BTN_START`/`BTN_SELECT` → start/select
- `BTN_MODE` → `PLAYOS_BUTTON_SYSTEM` (reserved)
- `KEY_PROG1`/`KEY_PROG2`/`KEY_LEFTMETA`/`KEY_RIGHTMETA`/`BTN_TRIGGER_HAPPY1` → `PLAYOS_BUTTON_QUICK_MENU` (reserved)

## Code Conventions

- C99. Link against libplayos, libwayland-client, libwayland-egl, libEGL, libGLESv2. (Raylib deferred.)
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

## Trusted IPC (Sprint 5)

The shell links against `playos-runtime`'s `trusted_control.h` (guarded by `PLAYOS_TRUSTED_IPC` preprocessor define). This provides:
- `playos_trusted_connect()` / `playos_trusted_disconnect()` — connection lifecycle
- `playos_trusted_shell_ready(int fd)` — fire-and-forget ShellReady notification (notifies init the shell is running)
- `playos_trusted_launch_game()`, `playos_trusted_terminate_game()`, `playos_trusted_shutdown()`, `playos_trusted_reboot()` — future operations

The ShellReady message is sent once during startup, after EGL init and Wayland registration, using a temporary connection (connect → send → close). No response is expected.
