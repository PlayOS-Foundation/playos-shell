# PlayOS Shell — Roadmap

Feature tracking and development plan for `playos-shell`, the reference
console UI for PlayOS runtime devices.

The shell is a **Wayland client** of the PlayOS compositor, built with
Raylib. All platform services go through the PlayOS Platform API — no
OS-specific code in the shell itself.

---

## ✅ Completed (v0.2)

### Core console loop
- [x] Game library screen with scrollable list
- [x] Controller navigation (D-Pad, A = launch, B = back, Home = overlay)
- [x] Launch game via `PlayOS::Runtime::LaunchAndWait` and return to shell
- [x] Fade-in / fade-out transition on launch

### Status bar
- [x] Battery level + icon (Remixicon, auto-hides when no battery)
- [x] WiFi icon (green/yellow/dim by connection state)
- [x] Bluetooth icon (blue when adapter present)
- [x] IP address display (bottom-right, for SSH debugging)
- [x] Controller connected indicator

### Platform compliance
- [x] All OS-specific code moved to Platform API backends (no `#ifdef __linux__`)
- [x] Battery: Linux sysfs + Windows `GetSystemPowerStatus`
- [x] Network: Linux ifaddrs + Windows `GetAdaptersAddresses`
- [x] Bluetooth: Linux sysfs + Windows `BluetoothFindFirstRadio`
- [x] Remixicon icon font (`remixicon.ttf`) with ASCII fallback

### Architecture
- [x] `PLAYOS_WINDOWED=1` env var for dev windowed mode
- [x] Font loaded selectively (only codepoints needed)
- [x] `SetExitKey(0)` — disable raylib default ESC→close (conflicts with overlay back)

### Input handling fixes
- [x] `H` key as keyboard fallback for Home button (opens overlay on VM/keyboard)
- [x] Vendor button mappings in evdev backend: `BTN_TRIGGER_HAPPY1`–`HAPPY4`, `KEY_PROG1` → Home, `KEY_PROG2` → QuickSettings (ROG Ally, Steam Deck)
- [x] Compositor modifier handler (`wlr_keyboard.events.modifiers`) — Shift/Ctrl forwarded to Wayland clients
- [x] WiFi password entry: raw-key shift-aware fallback (US layout) when GLFW char callback doesn't fire
- [x] WiFi password entry: prevent double characters (GetCharPressed + fallback both firing)

---

## ✅ In Progress → Done

### Shell refactor — Screen Stack
- [x] `IScreen` interface (`OnEnter`, `OnExit`, `Update`, `Draw`)
- [x] `ScreenStack` with push/pop
- [x] `StatusBar` as a persistent component (polls every 5 s)
- [x] `Icons` helper (load Remixicon once, draw by codepoint with fallback)
- [x] `LibraryScreen` — game list, launch, fade transitions
### Overlay menu
- [x] WiFi Settings
- [x] Install to Disk
- [x] Close (ESC / B / Home to dismiss)
- [x] `ShellApp` — owns window, loop, stack, status bar
- [x] `main.cpp` reduced to 6 lines

---

## 📋 Planned — Phase 2 (Settings)

### Configuration screen
- [ ] `ConfigScreen` — hub for system settings
- [x] `WiFiScreen` — scan for networks, connect with keyboard password entry
  - Uses `PlayOS::Network::ScanNetworks()` + `PlayOS::Network::Connect()`
  - Signal strength bars, lock icon for secured networks
  - Keyboard password entry with show/hide toggle (Tab)
  - Accessible via Home → WiFi Settings
- [ ] `BluetoothScreen` — scan for devices, pair controllers
- [ ] `DisplayScreen` — brightness (when `Capability::Brightness` present)
- [ ] `PowerScreen` — sleep / shutdown / restart

### Installer
- [x] `playos-installer-gui` — standalone raylib GUI application for disk installation
  - Spawned from Shell overlay menu: "Install to Disk" → launches `playos-installer-gui &`
  - Auto-detects internal disk (nvme0n1 / sda / vda / mmcblk0)
  - Confirmation screen: "This will ERASE ALL DATA. Continue?" with disk info
  - Forks `/usr/bin/playos-installer` shell script for backend
  - Polls `/run/playos/install-status` for real-time progress (stages + percentage)
  - Renders animated progress bar with stage labels
  - On complete: shows "Rebooting..." and triggers reboot via `reboot -f`
  - On failure: shows error with log path for debugging
  - Links against raylib only — no PlayOS API dependencies (lightweight, ~290 lines)
- [x] `/usr/bin/playos-installer` — standalone shell script (no OpenRC wrapper)
  - Updates APK repos → partitions with setup-disk → configures services → EFI cleanup
  - Writes progress stages to `/run/playos/install-status` for GUI polling
  - Copies PlayOS binaries, services, libs, samples, SSH keys to installed root
  - Handles NVMe/SATA/eMMC partition naming
- [x] Source: `playos-shell/src/installer/main.cpp` (GUI) + `playos-refdistro/alpine/install-script/playos-installer` (backend)

---

## 📋 Planned — Phase 2.5 (Device Profiles · RFC-0006)

> **Goal:** Shell loads device profile at startup for proper button mapping
> and device identification. Implementation lives in `playos-platform-api`
> and `playos-reference-devices`; the shell is a consumer.
>
> Spec: [`rfcs/0006-device-profile-format.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/rfcs/0006-device-profile-format.md)
>
> See also: [`playos-platform-api/ROADMAP.md`](https://github.com/PlayOS-Foundation/playos-platform-api/blob/main/ROADMAP.md)
> and [`playos-reference-devices/ROADMAP.md`](https://github.com/PlayOS-Foundation/playos-reference-devices/blob/main/ROADMAP.md)

- [ ] `ShellApp` loads profile from `/etc/playos/device-profiles/` or kernel cmdline
- [ ] `StatusBar` shows device name from profile
- [ ] Overlay shows device info (model, profile version)

---

## 📋 Planned — Phase 3 (Marketplace)

### Store integration
- [ ] `MarketplaceScreen` — browse available games
  - Fetches catalogue from PlayOS Cloud API
  - Paginated list with cover art
- [ ] `GameDetailScreen` — description, screenshots, ratings, install button
- [ ] Download + install progress indicator
- [ ] Installed games appear in LibraryScreen automatically

---

## 📋 Planned — Phase 4 (User & Cloud)

### Account and cloud saves
- [ ] User profile indicator in status bar (avatar / name)
- [ ] `AccountScreen` — sign in / out, profile
- [ ] Cloud save sync status per game
- [ ] Achievements unlock notifications (toast overlay)
- [ ] Friends list (future)

---

## 📋 Planned — Phase 5 (Polish)

> **Goal:** Console-grade feel — sound, motion, personality. Every item here
> makes the shell *feel* like a real device rather than a dev tool.

### Visuals & cover art
- [ ] **Game cover art** in library list — loaded from `.gpk` package or
  `/data/games/<id>/cover.png`, displayed as thumbnail next to each title
- [ ] **Library background** — subtle animated gradient or slow particle field
  behind the game list (replaces flat black, makes the shell feel alive)

### Screen transitions
- [ ] **Animated transitions** between screens — push = slide-from-right,
  pop = slide-to-right, 200 ms with `easeOutCubic` easing. `ScreenStack`
  manages transition state, renders both outgoing and incoming screens during
  the animation.
- [ ] **Loading spinner** — 12-frame arc animation (`DrawSpinner(x, y, r, color)`)
  shown in any screen during operations >500 ms (WiFi scan, connecting,
  installing). Reusable helper in `src/spinner.cpp`.

### UI sound system
- [ ] **`AudioManager`** — lightweight singleton that plays short WAV/OGG
  samples from `/usr/share/playos/sounds/`. Menu move (tick), confirm (click),
  back (cancel), overlay open (whoosh), game launch (swoosh), notification
  (chime), error (buzz). Volume respects `PlayOS::Audio::GetVolume()`.
  Toggle: "UI Sounds: ON/OFF" in overlay.
  Uses Raylib's `LoadSound`/`PlaySound` (already linked via
  `PlayOS::playos-audio-raylib`). ~150 lines in `src/audio_manager.cpp`.

### Theme system
- [ ] **Theme struct** — replace hardcoded `Color{18, 18, 28, 255}` with a
  `Theme` struct: `background, surface, surfaceBorder, accent, accentDim,
  textPrimary, textSecondary, textMuted, selected, danger, success, overlayDim`.
  A `gTheme` global owned by `ShellApp`, passed to every screen.
- [ ] **Dark + Light presets** — load from TOML at
  `/usr/share/playos/themes/dark.toml` and `light.toml`. Dark is default.
- [ ] **Theme toggle** in overlay: Home → Theme → Dark/Light.
- [ ] **Custom themes** — third-party themes loaded from
  `/usr/share/playos/themes/` (user-installed `.toml` files).

### Notification toasts
- [ ] **Toast queue** — non-blocking overlays sliding in from top-right, auto-dismiss
  after 3 s. Types: info, success, warning, error (colored left border).
  Stacked vertically when multiple queued.
  API: `m_toasts.Show("WiFi Connected", ToastType::Success);`.
  Used for: download complete, update available, controller disconnected,
  achievement unlock, error messages.

### Haptic feedback
- [ ] **Gamepad rumble** — short pulses via `PlayOS::Input` rumble API
  (if the device profile reports rumble support). D-Pad move: 20 ms light,
  confirm: 40 ms medium, back: 30 ms light, error: 50 ms sharp.
  Skipped silently on keyboard/VM. Toggle in overlay.

### Idle & power saving
- [ ] **Idle screen dim** — after 60 s of no input on `LibraryScreen`,
  gradually dim to 70 % brightness (prevents OLED burn-in on ROG Ally).
  Restore on any input. Resets on screen push/pop.

### Library features
- [ ] Search / filter in library (text input via on-screen keyboard or physical
  keyboard)
- [ ] Recently played / quick-resume section at top of library list
- [ ] Accessibility: larger text mode, high contrast toggle

---

## 💡 Ideas / Future

- Game collections / folders
- Parental controls (per-game time limits, rating filter)
- Remote Play indicator (stream a game to another device)
- Performance overlay (FPS, temps) via Home button shortcut
- Startup boot animation / splash screen
- Welcome wizard on first boot (WiFi, hostname, timezone, user account)
- Alternative shell support (the shell is a replaceable Wayland client)

---

## Architecture reference

See `playos-spec` book, chapter
[`09-shell-and-ux/16-reference-shell.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/book/src/09-shell-and-ux/16-reference-shell.md)
for the normative spec (what and why).

See `ARCHITECTURE.md` in this repo for the implementation guide
(how it is built, how to add a screen).
