> **⚠️ FIRST:** Read [`gen-context.md`](../gen-context.md) before anything else to understand the full PlayOS platform context.
# Copilot instructions — playos-shell

Reference **PlayOS Shell**: the controller-first console UI built with Raylib.
Source of truth is
[`playos-spec`](https://github.com/PlayOS-Foundation/playos-spec) (Part IX).
Also read `AGENTS.md`.

## Build & CI

```sh
# Standard build
cmake -B build && cmake --build build
./build/playos-shell

# Wayland build (Linux only, requires wayland-protocols + libxkbcommon-dev)
cmake -B build -DPLAYOS_SHELL_WAYLAND=ON && cmake --build build

# System Raylib (Alpine) instead of FetchContent
cmake -B build -DPLAYOS_USE_SYSTEM_RAYLIB=ON && cmake --build build

# Dev: force windowed mode (avoids fullscreen hijack)
PLAYOS_WINDOWED=1 ./build/playos-shell
```

CI runs `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
on both `ubuntu-latest` and `windows-latest` (no tests yet — this project is in
early scaffold). On CI, PlayOS deps (`playos-platform-api`, `playos-runtime`) are
fetched from GitHub; Raylib is fetched via FetchContent.

## Architecture

```
src/main.cpp                 Entry point (6 lines) — creates ShellApp
  └─ ShellApp                Owns window, screen stack, status bar, main loop
       ├─ ScreenStack        Push/pop stack of IScreen instances
       │   ├─ IScreen        Interface: OnEnter / OnExit / Update(dt) / Draw(W,H)
       │   ├─ LibraryScreen  Game list, navigation, launch with fade transitions
       │   ├─ OverlayScreen  Home-button overlay: WiFi, Install, Close
       │   ├─ WifiScreen     Network scan, signal bars, keyboard password entry
       │   └─ InstallerScreen zstd|dd disk image writer with progress bar
       ├─ StatusBar          Battery, WiFi, Bluetooth, IP, controller indicators
       │                     Polls Platform API every 5 s (not every frame)
       └─ Icons              Remixicon TTF glyph rendering with ASCII fallback
```

**Dependency resolution:** `playos_add_dependency()` in CMakeLists.txt first
checks sibling directories (`../playos-platform-api/`, `../playos-runtime/`),
then falls back to GitHub FetchContent. This enables local multi-repo development.

**Backend selection** is per-platform in CMakeLists.txt: Linux gets native
backends (battery-linux, network-linux, bluetooth-linux), Windows gets Win32
equivalents, everything else gets null backends.

## Key conventions

1. **Platform API for all OS interaction.** Input, storage, lifecycle, battery,
   network, Bluetooth — all go through `PlayOS::` APIs. Never use raw evdev,
   Win32, Wayland, or nmcli. Raylib is for rendering only.
2. **No `#ifdef` platform checks in public headers.** Use backend interfaces and
   `PlayOS::Capabilities::Has()` runtime queries instead. The CMakeLists.txt
   selects the right backends at link time.
3. **Controller-first.** Every action reachable with a gamepad (D-Pad nav,
   A = select, B = back, Home = overlay). Keyboard/mouse are dev conveniences.
4. **Launch via PlayOS::Runtime::LaunchAndWait.** Do not spawn processes ad hoc.
   The shell blocks while the game runs, then returns to the library.
5. **Shell is a Wayland client, not the compositor.** Keep it replaceable.
   The compositor lives in `playos-runtime/compositor/`.
6. **Device profiles** are TOML files at `/run/playos/profiles/<id>.toml`
   (staged to tmpfs by initramfs; never read from EROFS — tomlplus11 segfaults).
   `ShellApp` auto-detects hardware via DMI and loads the matching profile.
7. **Remixicon with ASCII fallback.** `Icons` loads `remixicon.ttf` from
   `/usr/share/playos/fonts/` and falls back to ASCII letters when absent.
   Each glyph has a pre-encoded UTF-8 constant in `Icons` (`Icons::Wifi`,
   `Icons::Battery`, etc.).
8. **SetExitKey(0).** Disable Raylib's default ESC→close; the overlay screen
   handles ESC as "back" within the screen stack.
9. **Fullscreen by default.** Set `PLAYOS_WINDOWED=1` env var for dev.
   `HideCursor()` is called at startup (console UX).

## Adding a new screen

1. Create `src/screens/<name>_screen.h` and `.cpp` implementing `IScreen`.
2. Wire it into the right parent screen — typically pushed via
   `m_stack.Push(std::make_unique<NewScreen>(...))`.
3. Update `CMakeLists.txt` to add the `.cpp` to `add_executable`.
4. Ensure controller navigation works (D-Pad + A/B bindings).
5. All platform data comes from `PlayOS::` APIs.
