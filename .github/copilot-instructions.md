# Copilot instructions — playos-shell

Reference **PlayOS Shell**: the controller-first console UI built with Raylib.
Source of truth is
[`playos-spec`](https://github.com/PlayOS-Foundation/playos-spec) (Part IX).
Also read `AGENTS.md`.

## Rules for changes here

1. **Platform services via the Platform API.** Input, storage, lifecycle go
   through `PlayOS::` APIs — not raw evdev/Win32/Wayland. Raylib is for
   rendering only.
2. **Controller-first.** Every action reachable with a gamepad; keyboard/mouse
   are dev conveniences.
3. **Launch via the Runtime.** Ask `PlayOS::Runtime` to launch games and detect
   exit; do not spawn processes ad hoc.
4. **A client, not the compositor.** Keep the shell replaceable; do not assume
   it owns the display.
5. **Keep it building** on Windows and Linux (Raylib via FetchContent; PlayOS
   deps from sibling checkouts or GitHub).

## Where things go

- Entry point + slice loop: `src/main.cpp`
- Build (deps, Wayland option): `CMakeLists.txt`
