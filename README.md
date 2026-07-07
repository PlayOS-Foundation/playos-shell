# playos-shell

The PlayOS reference console interface, built with **Raylib**, delivering a
controller-first experience for launching and managing applications.

Implements contracts from
[`playos-spec`](https://github.com/PlayOS-Foundation/playos-spec)
(Part IX, Shell and UX). The shell uses the **PlayOS Platform API** for
platform services (input, lifecycle) and the **PlayOS Runtime** to launch
games — it does not talk to the OS directly for those.

## Status

Early scaffold implementing the **vertical slice** on the Windows SDK target:

```text
Shell (game list) -> select -> launch -> play -> return to Shell
```

- Renders a simple library with controller/keyboard navigation.
- Reads input through `PlayOS::Input` (XInput on Windows).
- Launches a game with `PlayOS::Runtime::LaunchAndWait` and returns to the
  shell when it exits.

> On a runtime device the shell runs as a **Wayland client** of the PlayOS
> compositor and is displayed fullscreen. On Windows it is a plain window and
> the launch call blocks while the game runs (mirroring "shell hidden while
> the game is in the foreground").

## Building

Requires CMake >= 3.20, a C++17 compiler, and network access (Raylib is
fetched via CMake `FetchContent`). PlayOS dependencies are resolved from
sibling checkouts (`../playos-platform-api`, `../playos-runtime`) if present,
otherwise fetched from GitHub.

```sh
cmake -B build
cmake --build build
./build/playos-shell
```

## Controls

| Input | Action |
|---|---|
| D-Pad Up/Down (or Arrows) | Move selection |
| A (or Enter) | Launch selected |
| B (or Esc) | Quit shell |

## License

Code will be released under an OSI-approved license (MIT/Apache-2.0); the
specification in `playos-spec` is CC-BY-4.0.
