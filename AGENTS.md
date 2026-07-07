# AGENTS.md — playos-shell

Guidance for AI agents and contributors working in this repository.

## What this repository is

The reference **PlayOS Shell**: the controller-first console UI built with
Raylib. It implements Part IX of `playos-spec`; the specification is the
source of truth.

## Golden rules

1. **Use the Platform API for platform services.** Input, storage, and
   lifecycle go through `PlayOS::` APIs — not raw evdev, Win32, or Wayland.
   Raylib is used for rendering only.
2. **Controller-first.** Every action must be reachable with a gamepad.
   Keyboard/mouse are dev conveniences, not the primary path.
3. **Launch via the Runtime.** The shell asks `PlayOS::Runtime` to launch
   games and detect exit; it does not spawn processes ad hoc.
4. **The shell is a client, not the compositor.** Keep it replaceable. Do not
   assume it owns the display.
5. **Spec first.** UX contracts (bindings, navigation) live in `playos-spec`.

## Layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | Shell entry point and slice loop |
| `CMakeLists.txt` | Build; fetches Raylib, links PlayOS deps |

## Dependencies

- **Raylib** — fetched via CMake `FetchContent`.
- **playos-platform-api**, **playos-runtime** — resolved from sibling
  checkouts if present, else fetched from GitHub.

## Build

```sh
cmake -B build
cmake --build build
./build/playos-shell
```
