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

---

## ✅ In Progress → Done

### Shell refactor — Screen Stack
- [x] `IScreen` interface (`OnEnter`, `OnExit`, `Update`, `Draw`)
- [x] `ScreenStack` with push/pop
- [x] `StatusBar` as a persistent component (polls every 5 s)
- [x] `Icons` helper (load Remixicon once, draw by codepoint with fallback)
- [x] `LibraryScreen` — game list, launch, fade transitions
- [x] `OverlayScreen` — Home overlay (system info, back to dismiss)
- [x] `ShellApp` — owns window, loop, stack, status bar
- [x] `main.cpp` reduced to 6 lines

---

## 📋 Planned — Phase 2 (Settings)

### Configuration screen
- [ ] `ConfigScreen` — hub for system settings
- [ ] `WiFiScreen` — scan for networks, connect with password
  - Uses `PlayOS::Network::GetWiFiState()` to show current status
  - Triggers `nmcli` / NetworkManager via a runtime service call
- [ ] `BluetoothScreen` — scan for devices, pair controllers
  - Uses `PlayOS::Bluetooth::IsPresent()`
  - Triggers BlueZ scan via a runtime service call
- [ ] `DisplayScreen` — brightness (when `Capability::Brightness` present)
- [ ] `PowerScreen` — sleep / shutdown / restart

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

### UX improvements
- [ ] Status bar poll interval (every 5 s, not every frame)
- [ ] Animated transitions between screens (slide / fade)
- [ ] Game cover art in library list (loaded from package)
- [ ] Search / filter in library
- [ ] Recently played / quick-resume section
- [ ] Notification toast system (download complete, update available, etc.)
- [ ] Theme / accent colour (user-configurable)
- [ ] Accessibility: larger text mode, high contrast

---

## 💡 Ideas / Future

- Game collections / folders
- Parental controls (per-game time limits, rating filter)
- Remote Play indicator (stream a game to another device)
- Performance overlay (FPS, temps) via Home button shortcut
- Custom shell themes (third-party, loaded from `/usr/share/playos/themes/`)
- Alternative shell support (the shell is a replaceable Wayland client)

---

## Architecture reference

See `playos-spec` book, chapter
[`09-shell-and-ux/16-reference-shell.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/book/src/09-shell-and-ux/16-reference-shell.md)
for the normative spec (what and why).

See `ARCHITECTURE.md` in this repo for the implementation guide
(how it is built, how to add a screen).
