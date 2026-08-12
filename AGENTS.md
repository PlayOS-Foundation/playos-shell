# AGENTS.md — playos-shell

> **Implementation status:** 🟡 Partial implementation — Raylib 6.0 shell renders and navigates with frame-callback vsync via the custom `rcore_playos.c` backend (Sprint 5.5). Direct evdev input for all controller buttons including reserved SYSTEM/QUICK_MENU (auto-retry on device discovery failure). Manifest JSON parsing added for game names/versions/descriptions. Lifecycle events handled (TERMINATE, SUSPEND, RESUME). Per-call IPC pattern for game launch. Stub manifests in refdistro overlay.

This repository implements the **PlayOS shell** — a controller-first, fullscreen Raylib 6.0 application that runs permanently as a Wayland client (EGL/GLES2 driven by the vendored Raylib PlayOS backend). It is the UI the user sees at boot and between games: the home screen, game library, settings, and system overlay trigger.

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
├── main.c              ← Entry point: shell state, evdev input, lifecycle poll, screen update/draw loop
├── input.c             ← Direct evdev input: all standard buttons/axes + reserved SYSTEM/QUICK_MENU
├── screen_home.c       ← Home screen render + input (stub)
├── screen_library.c    ← Game library grid render + input (stub)
├── screen_game_detail.c← Game detail / launch confirm screen (stub)
├── screen_settings.c   ← Settings screens / tabbed (stub)
└── render_util.c       ← Thin Raylib wrappers (BeginDrawing/EndDrawing, DrawRectangleRec, DrawTextEx)

external/raylib/
└── src/platforms/rcore_playos.c  ← Raylib 6.0 PlayOS backend (Wayland/EGL/GLES2, frame pacing)

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

## Rendering (Sprint 5.5 — Raylib 6.0)

Rendering is done by **vendored Raylib 6.0** through a custom PlayOS platform backend, `external/raylib/src/platforms/rcore_playos.c` (registered as `PLATFORM_PLAYOS`). The shell no longer manages EGL surfaces/contexts directly.

The backend owns:
- Wayland connection + `wl_compositor`/`xdg_wm_base`/`playos_manager_v1` globals
- A fullscreen `xdg_toplevel` surface (no decorations, no resize)
- `wl_egl_window` + EGL/GLES2 context (made current before raylib's `rlgl` init)
- Frame pacing and buffer swap

`render_util.c` is a thin wrapper over raylib draw calls (`BeginDrawing`/`ClearBackground`, `DrawRectangleRec`, `DrawTextEx`/`MeasureTextEx` with `GetFontDefault()`). The retired 5×7 bitmap font and raw GLSL shaders are gone.

## Frame Callback Vsync

Frame pacing uses the standard Wayland vsync pattern inside `rcore_playos.c`'s `SwapScreenBuffer()` (called by raylib's `EndDrawing()`):
1. Request a frame callback via `wl_surface_frame(surface)`.
2. Commit the surface to trigger callback delivery.
3. Block in `wl_display_dispatch()` until the compositor signals readiness (callback fires, sets `frame_pending = false`).
4. `eglSwapBuffers()` on the Wayland EGL surface.

No busy-waiting, no `usleep` heuristics.

## Input (Sprint 5 — Direct Evdev)

All controller input is read **directly from evdev** (`/dev/input/event*`) by `shell_input_poll()` in `src/input.c`. This is the Sprint 5 pragmatic approach (S5-T5 option 1): the shell opens its own fd, decodes all standard buttons (face buttons, d-pad via both ABS_HAT and BTN_DPAD_* forms, shoulders, stick clicks) and reserved buttons (SYSTEM/QUICK_MENU), and does not depend on the Platform API input path (which strips reserved buttons and uses a separate fd with different detection criteria).

**Device detection:** `is_gamepad_device()` requires all 4 stick axes (`ABS_X`, `ABS_Y`, `ABS_RX`, `ABS_RY`) and `BTN_SOUTH` face button — matches the Platform API detection criteria in `backend_evdev.c`. No d-pad capability bits required (hid-asus on ROG Ally may not advertise them). **Device discovery:** `find_gamepad_device()` scans `/dev/input/event*`, prefers Xbox/ASUS/ROG Ally named devices, falls back to first viable gamepad. Auto-retries on each frame if the initial discovery failed (driver not yet loaded at startup).

Button mappings:
- `BTN_SOUTH`/`BTN_EAST`/`BTN_WEST`/`BTN_NORTH` → face buttons (A/B/X/Y)
- `BTN_DPAD_*` or `ABS_HAT0X/Y` → d-pad (both forms handled, mutually exclusive)
- `BTN_TL`/`BTN_TR` → L1/R1, `BTN_THUMBL`/`BTN_THUMBR` → L3/R3
- `BTN_START`/`BTN_SELECT` → start/select
- `BTN_MODE` → `PLAYOS_BUTTON_SYSTEM` (reserved)
- `KEY_PROG1`/`KEY_PROG2`/`KEY_LEFTMETA`/`KEY_RIGHTMETA`/`BTN_TRIGGER_HAPPY1` → `PLAYOS_BUTTON_QUICK_MENU` (reserved)

## Code Conventions

- C99. Link against libplayos and the vendored Raylib 6.0 static library (which itself links libwayland-client, libwayland-egl, libEGL, libGLESv2).
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
