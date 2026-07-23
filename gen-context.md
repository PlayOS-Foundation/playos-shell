# PlayOS Foundation — Generated Context

> **Auto-generated:** 2026-07-23  
> **Motto:** Write Once. Play Anywhere. Own Everything.  
> **License:** Spec = CC-BY-4.0 | Code = MIT/Apache-2.0  

---

## What is PlayOS?

PlayOS is an **open, specification-first console platform**. It defines a portable platform model for building, running, packaging, distributing, and supporting console-style games and applications across many devices, operating systems, and hardware profiles.

PlayOS is **not** a single game engine, Linux distribution, launcher, or marketplace. It is the **contract** that connects games, engines, runtimes, devices, stores, and cloud services.

---

## Platform Principles (9 Rules)

All design decisions are governed by these principles (RFC-0001):

1. **Specification First** — Everything is specified before implemented. The PlayOS Book is the source of truth.
2. **Engine Agnostic** — The Platform API never depends on a single game engine. Raylib is a reference only.
3. **Console First** — Controller-first UX, touch-aware. Keyboard/mouse are optional dev conveniences.
4. **Capability Based** — Apps query capabilities at runtime, never use `#ifdef` or platform checks.
5. **Runtime Independent** — The API works on full Runtime Devices and lightweight SDK Targets.
6. **Self-Hostable by Design** — Cloud, stores, and services must be runnable by communities/OEMs.
7. **Open Platform** — Open spec, open source, no gatekeeping.
8. **Long-Term Compatibility** — Stable APIs, additive changes, documented deprecation.
9. **Security and Trust** — Package signing, permissions, sandboxing, privacy by default.

---

## Repository Map

All repos live under `PlayOS-Foundation/` on GitHub and as siblings in `/home/nikmes/playos/`.

| Repository | Purpose | Status | Key Tech |
|---|---|---|---|
| **playos-spec** | Source of truth: specification book (174 chapters written), 19 RFCs, 4 ADRs, schemas | Active | mdBook, Markdown, JSON Schema, TOML |
| **playos-platform-api** | Reference implementation of the Platform API (engine-agnostic C++ library) | v0.3 | C++17, CMake, evdev/XInput/Raylib backends |
| **playos-runtime** | App lifecycle, process execution, wlroots compositor (C→C++ rewrite planned) | v0.3 | C++17, CMake, wlroots 0.19, TinyWL |
| **playos-shell** | Reference console UI (controller-first, Raylib). Integrated installer (dd pipeline), WiFi config, game library. | v0.3 | C++17, CMake, Raylib 5.x, Wayland client |
| **playos-refdistro** | Alpine-based reference OS. Pre-built compressed GPT disk image + bootable ISO. PXE-deployable. | v0.3 | Alpine 3.24, OpenRC, musl, systemd-nspawn, aports, mkimage |
| **playos-reference-devices** | Per-device device profiles (TOML), bring-up scripts | v0.2 | TOML profiles (ROG Ally, Generic Desktop, ASUS Ultrabook) |
| **playos-samples** | Official sample apps (hello-playos, space-invaders) | Active | C++17, CMake, Raylib + Platform API |
| **playos-cloud** | Self-hostable cloud: accounts, saves, achievements, analytics | Spec-only | Planned: PostgreSQL, MinIO, Keycloak |
| **playos-marketplace** | Open marketplace: publish, discover, install, update | Spec-only | Planned: .gpk catalog, multi-store |
| **playos-tools** | Dev tools: packaging, templates, build, deploy, SDK | Spec-only | Planned: .gpk packager, CLI |
| **playos-foundation** | Website, governance, roadmap, branding, community | Active | No platform code |

---

## Architecture Overview

```
┌─────────────────────────────────────────┐
│                GAMES / APPS              │
│   (Raylib + PlayOS::Platform API)        │
├─────────────────────────────────────────┤
│           PLAYOS SHELL (Raylib)          │  ← Wayland client, controller-first UI
│    Library | Installer | WiFi | Overlay  │     Installer: zstd|dd pipeline, GPT resize
├─────────────────────────────────────────┤
│        PLAYOS PLATFORM API (C++)         │  ← Engine-agnostic: input, lifecycle,
│   ┌──────────┬──────────┬──────────┐    │     storage, capabilities, cloud, etc.
│   │  evdev   │  XInput  │  Raylib  │    │
│   │ backend  │ backend  │ backend  │    │
│   └──────────┴──────────┴──────────┘    │
├─────────────────────────────────────────┤
│        PLAYOS RUNTIME / COMPOSITOR       │  ← wlroots 0.19, process supervision,
│         (Wayland compositor)             │     package execution, IPC
├─────────────────────────────────────────┤
│        REFERENCE OS (Alpine Linux)       │  ← musl, OpenRC, aports, mkimage
│               Linux Kernel               │
├─────────────────────────────────────────┤
│               HARDWARE                   │  ← ROG Ally (primary), Steam Deck, etc.
└─────────────────────────────────────────┘
```

### Two Target Categories (RFC-0002)

| | Runtime Device | SDK Target |
|---|---|---|
| **Runs full PlayOS?** | Yes (compositor, shell, packages) | No (API subset only) |
| **Examples** | ROG Ally, Steam Deck, Orange Pi | Windows, macOS, Android, PS Vita |
| **API access** | Full Platform API | Core subset (input, lifecycle, storage) |
| **Capability check** | `HasCapability(Capability::Overlay)` → true | Same call → false |

---

## Technology Stack

| Layer | Technology |
|---|---|
| **Language** | C++17 (all runtime code) |
| **Build** | CMake >= 3.20, Ninja |
| **Rendering (reference)** | Raylib 5.x (fetched via FetchContent) |
| **Compositor** | wlroots 0.19 (TinyWL-derived skeleton) |
| **Input** | evdev (Linux), XInput (Windows), Raylib polling (cross-plat) |
| **Reference OS base** | Alpine Linux 3.24 (musl, OpenRC) |
| **Image tooling** | Alpine aports + mkimage, systemd-nspawn (Ubuntu host) |
| **Container (optional)** | Docker (Alpine builder image) |
| **Package format** | `.gpk` (signed ZIP with manifest, see RFC-0005) |
| **Device profiles** | TOML (RFC-0006, schema at `schemas/device-profile.schema.json`) |
| **Documentation** | mdBook (The PlayOS Book) |
| **Spec repo** | `playos-spec/` — 17 parts, 238 chapters (174 written, 64 stubs), 19 RFCs, 4 ADRs |

---

## Key Architecture Decisions (ADRs)

| ADR | Decision |
|---|---|
| **0001** | Use mdBook for the PlayOS specification book |
| **0002** | Use wlroots/TinyWL as the Wayland compositor foundation |
| **0003** | Arch Linux as reference runtime base → **superseded by 0004** |
| **0004** | **Alpine Linux** replaces Arch as reference OS base (musl, apk, OpenRC) |

*No new ADRs since 2026-07-19.*

---

## Key RFCs

| RFC | Topic | Status |
|---|---|---|
| **0001** | Platform principles (9 rules, see above) | Accepted |
| **0002** | Runtime Devices vs SDK Targets | Accepted |
| **0003** | Capability model (no `#ifdef`, query at runtime) | Accepted |
| **0004** | Platform API surface (core portable + runtime-only) | Accepted |
| **0005** | `.gpk` package format (ZIP + `manifest.json` + signature) | Accepted |
| **0006** | Device profile format (TOML, per-device input/caps/display) | Accepted |
| **0007** | Cloud & marketplace architecture (self-hostable, multi-tenant) | Accepted |
| **0008** | Game lifecycle contract (launch, crash, exit state machine) | Draft |
| **0009** | Security, sandboxing & trust model | Draft |
| **0010** | Certification & conformance program | Draft |
| **0011** | Governance, versioning & compatibility model | Draft |
| **0012** | Engine integration contract | Draft |
| **0013** | IPC & runtime service architecture | Draft |
| **0014** | Device porting & bring-up model | Draft |
| **0015** | Observability, telemetry & privacy | Draft |
| **0016** | Self-hosting & store federation | Draft |
| **0017** | Developer experience & SDK model | Draft |
| **0018** | Accessibility as a platform requirement | Draft |
| **0019** | Update & patch distribution model | Draft |

RFCs 0001–0007 are accepted and drive implementation. RFCs 0008–0019
are draft specifications providing design direction for Parts XIII–XVI
of the PlayOS Book. They are staged for review and formal acceptance.

---

## Current Development Status

### v0.3 — Book Foundation & API Stabilization

The specification foundation is laid: **174 chapters written** across Parts I–XII
(Front Matter through Device Model & Porting), with **64 stubs remaining** in
Parts XIII–XVI and Appendices.

The **console loop** works end-to-end:

```
Shell (game list) → select → launch → play → return to Shell
```

- **playos-shell**: Library screen (scans `/data/games/`), gamepad navigation, status bar (battery/WiFi/BT), overlay (WiFi, Install, Close), `InstallerScreen` (native C++ `zstd | dd` pipeline, disk size validation, GPT partition resize, progress bar), screen stack architecture. Old `src/installer/` and standalone `playos-installer-gui` removed.
- **playos-platform-api**: Capabilities, Input (evdev/XInput/Raylib), Lifecycle, Storage, Display, Audio, Battery (sysfs/Win32), Network (nmcli/Win32), Bluetooth, Touch — all with swappable backends. 9 backend interfaces with Linux, Windows, Raylib, and null implementations.
- **playos-runtime**: `PlayOS::Runtime::LaunchAndWait` (Windows + POSIX), wlroots compositor skeleton (DRM/KMS display, keyboard input, XDG toplevel)
- **playos-samples**: `hello-playos` and `space-invaders` (axis input, not digital)
- **playos-refdistro**: Alpine 3.24 image profile. Pre-built compressed disk image (3-partition GPT: ESP 512M + root 4G + data fills remainder). `playos-firstboot` regenerates machine-id, UUIDs (root/EFI/data), updates fstab/boot entries, cleans stale EFI entries, self-deletes. Shell-script installer removed. Build scripts: `build-disk-image.sh`, `build-iso-ubuntu.sh`, `test-disk-image-qemu.sh`, `test-iso-qemu.sh`. PXE deployment functional. ROADMAP.md tracks migration progress.

### Book Completion Status

| Parts | Status | Chapters |
|---|---|---|
| 00–Front Matter | ✅ Complete | 6/6 |
| 01–Vision | ✅ Complete | 7/7 |
| 02–Platform Principles | ✅ Complete | 9/9 |
| 03–Platform Architecture | ✅ Complete | 12/12 |
| 04–Target Model | ✅ Complete | 8/8 |
| 05–Capability Model | ✅ Complete | 10/10 |
| 06–Platform API | ✅ Complete | 25/25 |
| 07–Engine Integration | ✅ Complete | 9/9 |
| 08–Runtime Architecture | ✅ Complete | 18/18 |
| 09–Shell & UX | ✅ Complete | 16/16 |
| 10–Package Format | ✅ Complete | 16/16 |
| 11–Cloud & Marketplace | ✅ Complete | 19/19 |
| 12–Device Model & Porting | ✅ Complete | 18/18 |
| 13–Security, Privacy & Trust | ⬜ Stubs only | 0/11 |
| 14–Certification | ⬜ Stubs only | 0/12 |
| 15–Developer Guide | ⬜ Stubs only | 0/18 |
| 16–Governance & Process | ⬜ Stubs only | 0/12 |
| 99–Appendices | ⬜ Stubs only | 0/11 |

### Backend Coverage Matrix (from Part XII)

| Module | Linux | Windows | Raylib | Android | PS Vita |
|---|---|---|---|---|---|
| Input | ✅ evdev | ✅ XInput | ✅ | ❌ | ❌ |
| Display | ✅ Raylib+env | ❌ Null | ✅ | ❌ | ❌ |
| Audio | ❌ **Missing** | ❌ **Missing** | ✅ | ❌ | ❌ |
| Battery | ✅ sysfs | ✅ Win32 | ❌ Null | ❌ | ❌ |
| Network | ✅ nmcli+getifaddrs | ⚠️ Partial | ❌ Null | ❌ | ❌ |
| Storage | ✅ XDG | ✅ Win32 | ❌ Null | ❌ | ❌ |
| Touch | ❌ Null | ❌ **Missing** | ✅ | ❌ | ❌ |
| Bluetooth | ✅ sysfs | ✅ Partial | ❌ Null | ❌ | ❌ |
| Brightness | ❌ **Missing** | ❌ **Missing** | ❌ Null | ❌ | ❌ |

### Key Limitations (v0.3)

- No pointer/touch input in compositor (keyboard only)
- No audio (Linux PipeWire backend not implemented)
- No suspend/resume
- No marketplace integration
- No update mechanism (planned Phase 8 — update screen + background service)
- No first-boot welcome wizard (planned Phase 5 — WiFi/hostname/timezone in Shell)
- Compositor is C skeleton, needs C++ RAII rewrite
- No Android or PS Vita backends (SDK Targets are spec-only)
- No brightness control backend on any platform
- No A/B atomic updates (planned Phase 8b)
- No image signature verification

---

## Primary Reference Hardware

**ASUS ROG Ally** (AMD Radeon 780M, x86_64) — Stage 1 bring-up in progress.
Device profile at `playos-reference-devices/rog-ally/device-profile.toml`.

Additional reference profiles: **Generic Desktop / VM**, **ASUS Ultrabook (NVIDIA)**.

Upcoming: Steam Deck, Orange Pi (ARM SBC), Windows/macOS/Android SDK targets, PS Vita SDK target.

---

## Repository Dependency Graph

```
playos-spec (source of truth)
    ↓ specifies contracts for
    ├── playos-platform-api ──────────────┐
    ├── playos-runtime (+ compositor) ────┤
    ├── playos-shell ─────────────────────┤ links Platform API + Runtime
    ├── playos-samples ───────────────────┤
    ├── playos-cloud ─────────────────────┤
    ├── playos-marketplace ───────────────┤
    ├── playos-tools ─────────────────────┘
    ├── playos-refdistro (builds ISO)
    └── playos-reference-devices (profiles + bring-up)
```

**Build-time resolution:** Each repo resolves sibling checkouts via relative paths first, then falls back to GitHub `FetchContent`. Expected layout is flat siblings in `/home/nikmes/playos/`.

---

## Build Commands (Quick Reference)

### Platform API
```sh
cd playos-platform-api && cmake -B build && cmake --build build
```

### Runtime + Compositor
```sh
cd playos-runtime && cmake -B build -DPLAYOS_BUILD_COMPOSITOR=ON && cmake --build build
```

### Shell
```sh
cd playos-shell && cmake -B build && cmake --build build
```

### Samples
```sh
cd playos-samples && cmake -B build && cmake --build build
```

### Alpine ISO + Disk Image (on this Ubuntu server)
```sh
cd playos-refdistro
bash scripts/setup-ubuntu-build-host.sh    # first time only
bash scripts/build-iso-ubuntu.sh           # full pipeline: image + ISO + PXE deploy
bash scripts/build-disk-image.sh           # disk image only (inside nspawn)
bash scripts/test-disk-image-qemu.sh out/playos-gpt-*.img.zst   # QEMU boot test
bash scripts/test-iso-qemu.sh              # QEMU ISO boot test
```

Outputs in `out/`:
- `playos-gpt-v3.24-x86_64.img.zst` — compressed disk image (~1.1 GiB, 3-partition GPT)
- `alpine-playos-v3.24-x86_64.iso` — bootable ISO (~1.9 GiB)
- Both auto-deployed to PXE server at `/var/www/html/playos/`

---

## Where Things Go (Decision Guide)

| If you need to... | Go to... |
|---|---|
| Define platform behavior | `playos-spec/book/` or `playos-spec/rfcs/` |
| Record an architecture decision | `playos-spec/adr/` |
| Add a Platform API function | `playos-platform-api/include/playos/` + `src/` |
| Add an OS backend | `playos-platform-api/src/backends/<platform>/` |
| Change how apps launch | `playos-runtime/src/process*.cpp` |
| Change the compositor | `playos-runtime/compositor/playos-compositor.c` |
| Change the console UI | `playos-shell/src/` |
| Add a new device | `playos-reference-devices/<device>/` + profile |
| Change the reference OS | `playos-refdistro/alpine/` (overlay, firstboot) or `playos-refdistro/scripts/` (build) |
| Change the installer | `playos-shell/src/screens/installer_screen.cpp` |
| Add update checking UI | `playos-shell/src/screens/` (new `update_screen.cpp`) — Phase 8 |
| Change first-boot behavior | `playos-refdistro/alpine/init.d/playos-firstboot` |
| Add a sample | `playos-samples/<sample>/` |
| Document governance | `playos-foundation/` |

---

## Agent Work Guidelines

1. **Always check `playos-spec` first** before implementing — the specification is the source of truth.
2. **Never add `#ifdef` platform checks** to public headers — use backend interfaces and capabilities.
3. **Keep the shell engine-agnostic** — all OS interaction goes through `PlayOS::` Platform API.
4. **No engine dependency in Platform API** — Raylib is only used in shell, samples, and as a reference backend.
5. **Cross-platform** — CMake selects backends; code compiles on Linux and Windows.
6. **Resolve sibling repos** — repos assume flat layout; check `../playos-*/` before FetchContent.
7. **Respect ADR-0004** — Alpine/musl is the only active reference distro. Arch is archived in Git history.
8. **Read the AGENTS.md** in each repo before working in it — each repo has its own golden rules.
9. **Use capability checks, not platform checks** — `Capabilities::Has(Capability::Touch)` not `#ifdef __ANDROID__`.
10. **Device profiles are TOML, package manifests are JSON** — `manifest.json` per RFC-0005, not `manifest.toml`.
11. **Distinguish Runtime Devices from SDK Targets** — full shell vs API subset only.
12. **Book chapters follow a pattern** — Title → Content sections → `## Related` with relative links.
13. **When in doubt, read the glossary** — `book/src/00-front-matter/06-glossary.md` has all terminology.
14. **Installation = pre-built disk image** — PlayOS ships a compressed GPT image written via `zstd | dd`. No shell-script installer, no network at install time. See `playos-refdistro/ROADMAP.md`.
15. **Update mechanism is planned, not built** — Phase 8 in ROADMAP.md. Update screen in Shell, background `playos-update` service, version manifest server.
