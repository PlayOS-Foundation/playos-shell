# Raylib 5.5 → 6.0 migration notes

This file records the concrete breaking changes that the PlayOS shell had to
reconcile when upgrading its vendored Raylib from 5.5 to 6.0. It is referenced
by the Sprint 5.5 commits (S5.5-T1..T3).

Source: vendored `external/raylib/CHANGELOG` (6.0 section), `src/config.h`,
`src/rcore.c`, and `src/platforms/rcore_template.c` / `rcore_drm.c`.

## 1. Platform-backend contract (`rcore.c` → `PLATFORM_*`)

The platform backend is included by `rcore.c` through a plain
`#if defined(PLATFORM_X)` dispatch (NOT through a `SUPPORT_PLATFORM_*` flag in
`config.h`). The PlayOS backend registers itself as `PLATFORM_PLAYOS` and adds a
branch + `#include "platforms/rcore_playos.c"` in `rcore.c`.

The functions actually **called** by `rcore.c` 6.0 are:

- `int InitPlatform(void)`
- `void ClosePlatform(void)`
- `bool WindowShouldClose(void)`
- `void SwapScreenBuffer(void)`
- `double GetTime(void)`
- `void PollInputEvents(void)`
- `Vector2 GetWindowScaleDPI(void)`
- `void MaximizeWindow(void)`
- `void MinimizeWindow(void)`
- `void SetWindowSize(int width, int height)`

`rcore_template.c` additionally lists many optional window/monitor/input
functions (`SetWindowTitle`, `GetWindowHandle`, `ToggleFullscreen`,
`SetClipboardText`, cursor functions, gamepad/mouse functions, `OpenURL`, …).
They are not called by `rcore.c` itself but are safe/expected for a complete
backend. The shell implements the full template surface with no-ops where the
Wayland/EGL-only shell has no concept (clipboard, cursor, monitors, gamepad).

Notable contract changes vs 5.5:

- **`CORE.Window.fullscreen` was REMOVED** (6.0 CHANGELOG). Backends now use the
  `FLAG_FULLSCREEN_MODE` window flag. PlayOS uses `xdg_toplevel_set_fullscreen`
  directly and does not rely on the removed variable.
- **`SetupFramebuffer()` was REMOVED** from the backend surface. The DRM backend
  (and PlayOS) no longer provide it.
- **`TRACELOGD()` macro was REMOVED** (hardly used debug macro).
- `InitGraphicsDevice` / `CloseGraphicsDevice` are still declared in
  `rcore_template.c` but are **not called anywhere in `rcore.c` 6.0** — the
  legacy two-phase init is gone; `InitPlatform()` owns both window and GL setup.

### GLES2 contract (from `rcore_drm.c`, the closest EGL/GLES2 reference)

- `GRAPHICS_API_OPENGL_ES2` (or `ES3`) must be defined for `rlgl.h`.
- `InitPlatform()` must: set `CORE.Window.*`/`CORE.Input.*` basics, call
  `rlLoadExtensions(eglGetProcAddress)`, call `InitTimer()`, and set
  `CORE.Storage.basePath = GetWorkingDirectory()`.
- EGL sequence: `eglGetPlatformDisplay` → `eglInitialize` → `eglChooseConfig`
  (EGL_OPENGL_ES2_BIT, RGBA8, depth) → `eglBindAPI(EGL_OPENGL_ES_API)` →
  `eglCreateContext` (client version 2) → `eglCreateWindowSurface` →
  `eglMakeCurrent`.
- `SwapScreenBuffer()` = `eglSwapBuffers(device, surface)`.
- `ClosePlatform()` = destroy surface/context, `eglTerminate`,
  `wl_egl_window_destroy`, `wl_surface_destroy`, `wl_display_disconnect`.
- `GetTime()` = `clock_gettime(CLOCK_MONOTONIC)` minus `CORE.Time.base`.

## 2. `config.h` / `SUPPORT_MODULE_*` flag changes

- `config.h` is now wrapped in `#if !defined(EXTERNAL_CONFIG_FLAGS)`: when that
  macro is defined, the build must supply every flag explicitly. The shell does
  not define `EXTERNAL_CONFIG_FLAGS` and edits `config.h` directly.
- Module selection flags keep the same names (`SUPPORT_MODULE_RSHAPES`,
  `SUPPORT_MODULE_RTEXTURES`, `SUPPORT_MODULE_RTEXT`,
  `SUPPORT_MODULE_RMODELS`, `SUPPORT_MODULE_RAUDIO`).
- **`SUPPORT_CAMERA_SYSTEM`** is the new flag for the split-out rcamera system
  (previously part of rcore). PlayOS disables it.
- Each `SUPPORT_MODULE_*` value is used by `#if` guards inside the corresponding
  `.c` file (`raudio.c`, `rmodels.c`, …), so setting it to `0` compiles the
  translation unit to an effectively empty object.

PlayOS configuration (rendering-only):

```c
#define SUPPORT_MODULE_RSHAPES   1
#define SUPPORT_MODULE_RTEXTURES 1
#define SUPPORT_MODULE_RTEXT     1   // requires RTEXTURES (sprite-font textures)
#define SUPPORT_MODULE_RMODELS   0   // 3D unused by shell
#define SUPPORT_MODULE_RAUDIO    0   // audio unused by shell
#define SUPPORT_CAMERA_SYSTEM    0
```

## 3. Renamed / removed symbols (5.5 → 6.0)

Only entries relevant to the shell (2D shapes/text only; no models/audio):

- `[rcore]` REMOVED `CORE.Window.fullscreen` variable.
- `[rcore]` REMOVED `SetupFramebuffer()`.
- `[rcore]` REMOVED `TRACELOGD()` macro.
- `[rcore]` RENAMED `SHADER_LOC_VERTEX_INSTANCETRANSFORMS` (internal shader loc).
- `[rcore]` REDESIGNED fullscreen modes (use current display resolution).
- `[rlgl]` REMOVED `RLGL_RENDER_TEXTURES_HINT` (always enabled now).
- `[rlgl]` REDESIGNED shader-loading API names (internal `LoadShader*`).
- `[rshapes]` REDESIGNED `DrawCircleGradient()` to take `Vector2 center` instead
  of `(float centerX, float centerY)`.
- `[rtext]` REDESIGNED `LoadFontData()` — added an input parameter.
- `[rtextures]` REMOVED SVG loading/drawing (moved to raylib-extras).
- `[rmodels]` REMOVED `DrawModelPoints()`/`DrawModelPointsEx()` (disabled module).
- `[external]` RENAMED `rl_gputex.h` → `rltexgpu.h`.

The shell's own code never used the removed symbols; the only surface it touches
in 6.0 is: `InitWindow`, `CloseWindow`, `WindowShouldClose`, `BeginDrawing`,
`EndDrawing`, `ClearBackground`, `DrawRectangle`, `DrawRectangleRec`,
`DrawTextEx`, `MeasureTextEx`, `GetFontDefault`, `GetScreenWidth`,
`GetScreenHeight`, `SetTargetFPS`, `GetTime`.
