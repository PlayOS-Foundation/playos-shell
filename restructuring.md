# PlayOS Shell — Architectural Restructuring

> **Goal:** Prepare the codebase for Phase 5 polish (themes, sound, transitions,
> toasts, haptics) without over-engineering. The key architectural bet is
> `AppContext` — a single handle passed to every screen that bundles all
> services. If that works, everything else slots in cleanly.

---

## Sprint 1 — Foundation: AppContext + Theme + new layout

> **Outcome:** Files moved to new directory layout. `AppContext` struct
> exists and is passed to every screen. `Theme` struct with dark preset
> replaces all hardcoded colors. Build still passes on Linux + Windows.

### T1.1 — Create new directory structure

Create the new directories and move existing files. No code changes yet.

```
src/
  main.cpp
  shell_app.cpp / shell_app.h

  app_context.h                     # NEW — empty struct for now

  core/
    screen.h                        # moved from src/screen.h
    screen_stack.cpp / .h           # moved from src/screen_stack.cpp/.h

  ui/
    icons.cpp / .h                  # moved from src/icons.cpp/.h
    status_bar.cpp / .h             # moved from src/status_bar.cpp/.h

  screens/
    library_screen.cpp / .h         # moved
    overlay_screen.cpp / .h         # moved
    wifi_screen.cpp / .h            # moved
    installer_screen.cpp / .h       # moved
```

- [x] Create directories: `src/core/`, `src/ui/`
- [x] Move files to their new locations
- [x] Update all `#include` paths to match new locations
- [x] Update `add_executable` paths in `CMakeLists.txt`
- [x] Build and verify: `cmake -B build && cmake --build build`

### T1.2 — Define `Theme` struct with dark preset

Create `src/ui/theme.h` and `src/ui/theme.cpp`. Define a `Theme` struct
with named colour fields matching every hardcoded colour currently used.
Provide a static `Theme::Dark()` factory for the default preset (same
values as the current hardcoded colours — no visual change).

- [x] Create `src/ui/theme.h` with `Theme` struct
  - Fields: `background`, `surface`, `surfaceBorder`, `accent`, `textPrimary`,
    `textSecondary`, `textMuted`, `selected`, `overlayDim`, `danger`, `success`
  - `static Theme Dark()` returning the current colour values
- [x] Create `src/ui/theme.cpp` with `Theme::Dark()` implementation
- [x] Declare `extern Theme gTheme` in `theme.h`
- [x] Define `Theme gTheme` in `theme.cpp` (initialised to `Theme::Dark()`)
- [x] Update `CMakeLists.txt` to include `src/ui/theme.cpp`
- [x] Build and verify

### T1.3 — Replace all hardcoded colours with `gTheme`

Replace every `Color{...}` literal in all `Draw()` methods with the
corresponding `gTheme.*` field. Mappings:

| Old hardcoded value | Theme field |
|---|---|
| `Color{12, 12, 18, 255}` | `gTheme.background` |
| `Color{8, 8, 12, 255}` | `gTheme.background` |
| `Color{18, 18, 28, 255}` | `gTheme.surface` |
| `Color{60, 60, 100, 255}` | `gTheme.surfaceBorder` |
| `Color{64, 130, 220, 255}` | `gTheme.accent` |
| `RAYWHITE` / `Color{200, 200, 210, 255}` | `gTheme.textPrimary` |
| `Color{120, 120, 140, 255}` | `gTheme.textSecondary` |
| `Color{80, 80, 100, 255}` | `gTheme.textMuted` |
| `Color{44, 52, 68, 255}` | `gTheme.selected` |
| `Color{0, 0, 0, 180}` | `gTheme.overlayDim` |
| `Color{180, 180, 220, 255}` | `gTheme.textPrimary` |
| `Color{160, 160, 180, 255}` | `gTheme.textSecondary` |
| `Color{130, 150, 130, 255}` | `gTheme.success` |
| `Color{40, 40, 70, 255}` | `gTheme.surfaceBorder` |

- [x] Update `library_screen.cpp` — ClearBackground, selection highlight,
  icon placeholder, text colours, scrollbar, status/help lines
- [x] Update `overlay_screen.cpp` — dim rectangle, panel, title, menu items,
  help text
- [x] Update `wifi_screen.cpp` — all colours
- [x] Update `installer_screen.cpp` — all colours
- [x] Update `status_bar.cpp` — all colours
- [x] Remove `#include` of theme header from each file
- [x] Build and verify — no visual change, just colour source moved

### T1.4 — Introduce `AppContext` and pass it to all screens

- [x] Create `src/core/app_context.h` with initial struct:
  ```cpp
  struct AppContext {
      ScreenStack& stack;
      Theme        theme{Theme::Dark()};
  };
  ```
- [x] Change every screen constructor from `(ScreenStack& stack)` to
  `(AppContext& ctx)`, store `AppContext& m_ctx`
- [x] Update all `m_stack.*` calls to `m_ctx.stack.*`
- [x] Update `ShellApp` to create `AppContext m_ctx{m_stack}` and
  pass `m_ctx` to `LibraryScreen(m_ctx)`
- [x] Build and verify

### T1.5 — Add `Theme` to `AppContext`

- [x] Add `Theme theme{Theme::Dark()};` to `AppContext`
- [x] Screens access theme via `m_ctx.theme.*` instead of `gTheme.*`
- [x] Remove `extern Theme gTheme` — theme is now context-scoped
- [x] Build and verify

---

## Sprint 2 — Toast notifications

> **Outcome:** Non-blocking toast notifications slide in from top-right,
> auto-dismiss after 3 seconds. Queued when multiple fire at once.

### T2.1 — Create `ToastManager`

- [ ] Create `src/ui/toast_manager.h`:
  ```cpp
  enum class ToastType { Info, Success, Warning, Error };

  class ToastManager {
  public:
      void Show(const std::string& msg, ToastType type = ToastType::Info);
      void Update(float dt);
      void Draw(int W, int H) const;
  private:
      struct Toast { std::string msg; ToastType type; float timer; };
      std::vector<Toast> m_queue;
      static constexpr float kDuration = 3.0f;
  };
  ```
- [ ] Create `src/ui/toast_manager.cpp`:
  - `Show()` pushes to `m_queue`
  - `Update()` decrements timers, removes expired toasts
  - `Draw()` renders stacked toasts at top-right with slide-in animation
    (translate based on remaining time for first 200ms). Colored left border
    per type: blue=info, green=success, yellow=warning, red=error.
- [ ] Update `CMakeLists.txt`
- [ ] Build and verify

### T2.2 — Integrate ToastManager into AppContext + ShellApp

- [ ] Add `ToastManager& toasts;` to `AppContext`
- [ ] `ShellApp` owns `ToastManager m_toastManager` and passes it via
  `AppContext{m_stack, m_theme, m_toastManager}`
- [ ] Add `m_toastManager.Update(dt)` to main loop
- [ ] Add `m_toastManager.Draw(W, H)` after `m_stack.Draw(W, H)` and
  before `m_statusBar.Draw(W, H)`
- [ ] Build and verify

### T2.3 — Wire toasts into existing screens

- [ ] `WiFiScreen`: show "Connected to <SSID>" success toast on connect,
  "Connection failed" error toast on failure
- [ ] `InstallerScreen`: show "Installation complete — rebooting" success
  toast, "Installation failed — see log" error toast
- [ ] Build and verify

---

## Sprint 3 — UI Audio

> **Outcome:** UI sound effects for navigation, confirm, back, overlay,
> launch, and errors. Volume respects Platform API. Togglable.

### T3.1 — Create `AudioManager`

- [ ] Create `src/audio/audio_manager.h`:
  ```cpp
  enum class AudioEvent { MenuMove, Confirm, Back, OverlayOpen,
                          GameLaunch, Notification, Error };

  class AudioManager {
  public:
      // Load sounds from /usr/share/playos/sounds/.
      // Graceful if directory or files are absent.
      bool Load(const std::string& soundDir);

      void Play(AudioEvent event);
      void SetEnabled(bool on) { m_enabled = on; }
      bool IsEnabled() const { return m_enabled; }
      void Update(float dt);  // nothing needed currently, placeholder

  private:
      // One Sound per event, or reuse with different pitch/volume.
      bool m_enabled = true;
      // Raylib Sound handles per event
  };
  ```
- [ ] Create `src/audio/audio_manager.cpp`:
  - `Load()` loads WAV files from `soundDir`: `move.wav`, `confirm.wav`,
    `back.wav`, `overlay.wav`, `launch.wav`, `notify.wav`, `error.wav`
  - `Play()` calls `PlaySound()` on the corresponding handle. Apply a
    50ms cooldown on `MenuMove` to prevent sound spam on rapid scrolling.
  - Graceful fallback: if any sound fails to load, that event is silent.
- [ ] Update `CMakeLists.txt`
- [ ] Build and verify

### T3.2 — Integrate AudioManager into AppContext + ShellApp

- [ ] Add `AudioManager& audio;` to `AppContext`
- [ ] `ShellApp` owns `AudioManager m_audioManager`, calls
  `m_audioManager.Load("/usr/share/playos/sounds/")` during init,
  passes via `AppContext{...}`
- [ ] Add `m_audioManager.Update(dt)` to main loop
- [ ] Build and verify

### T3.3 — Wire sounds into screens

- [ ] `LibraryScreen::Update()`: call `m_ctx.audio.Play(MenuMove)` on D-Pad,
  `m_ctx.audio.Play(Confirm)` on A, `m_ctx.audio.Play(GameLaunch)` on launch
- [ ] `OverlayScreen::Update()`: `m_ctx.audio.Play(OverlayOpen)` on enter,
  `m_ctx.audio.Play(Confirm)` on select, `m_ctx.audio.Play(Back)` on close
- [ ] `WiFiScreen::Update()`: `m_ctx.audio.Play(Confirm)` on connect,
  `m_ctx.audio.Play(Error)` on failure
- [ ] All screens: `m_ctx.audio.Play(Back)` on B press
- [ ] Build and verify

---

## Sprint 4 — ScreenStack transitions

> **Outcome:** Push = slide-from-right, Pop = slide-to-right. 200ms,
> easeOutCubic. Screens require zero changes — transitions live entirely
> inside `ScreenStack`.

### T4.1 — Add transition state to ScreenStack

- [ ] Add to `screen_stack.h`:
  ```cpp
  enum class TransitionDir { None, Push, Pop };
  ```
- [ ] Add private members:
  ```cpp
  TransitionDir m_transition = TransitionDir::None;
  std::unique_ptr<IScreen> m_outgoing;
  float m_transitionTimer = 0.0f;
  static constexpr float kTransitionDuration = 0.2f;  // 200 ms
  ```
- [ ] Modify `Push()`:
  - Before pushing, if stack is non-empty, store current top as
    `m_outgoing` and set `m_transition = Push`
  - Call `OnEnter()` on the new screen after pushing
- [ ] Modify `Pop()`:
  - Store current top as `m_outgoing` and set `m_transition = Pop`
  - Pop from vector
  - After transition completes, call `OnExit()` on `m_outgoing` and free it
- [ ] Modify `Update(dt)`:
  - If transition active, advance `m_transitionTimer += dt`
  - Update only the incoming (top) screen during transition
  - When timer exceeds `kTransitionDuration`, clear transition state
- [ ] Modify `Draw(W, H)`:
  - If no transition: draw top screen normally (unchanged)
  - If Push transition: draw outgoing at `(0,0)`, draw incoming offset
    from right: `x = W * (1.0 - easeOutCubic(t))`
  - If Pop transition: draw incoming at `(0,0)`, draw outgoing sliding
    right: `x = W * easeOutCubic(t)`
  - `easeOutCubic(t) = 1 - pow(1 - t, 3)` where `t = timer / duration`
- [ ] Build and verify — push/pop should animate, screens need zero changes

---

## Sprint 5 — InputManager + idle tracking

> **Outcome:** Centralised input layer with idle timer. Screen input helpers
> move here. Unblocks idle dim and haptics.

### T5.1 — Create InputManager

- [ ] Create `src/core/input_manager.h`:
  ```cpp
  class InputManager {
  public:
      void Update(float dt);

      // Navigation (same as current per-screen helpers)
      bool PressedUp()    const;
      bool PressedDown()  const;
      bool PressedConfirm() const;
      bool PressedBack()  const;
      bool PressedHome()  const;

      // Idle tracking
      float IdleTime() const { return m_idleTimer; }
      bool  IsIdleFor(float seconds) const { return m_idleTimer >= seconds; }

  private:
      bool AnyInputThisFrame() const;  // check all buttons + keyboard
      float m_idleTimer = 0.0f;
  };
  ```
- [ ] Create `src/core/input_manager.cpp`:
  - `Update(dt)`: if `AnyInputThisFrame()`, reset `m_idleTimer = 0`;
    otherwise `m_idleTimer += dt`
  - `Pressed*()` methods wrap `PlayOS::Input::Pressed(Button::*)` with
    keyboard fallbacks, same as current per-screen helpers
- [ ] Update `CMakeLists.txt`
- [ ] Build and verify

### T5.2 — Integrate InputManager into AppContext, remove per-screen helpers

- [ ] Add `InputManager& input;` to `AppContext`
- [ ] `ShellApp` owns `InputManager m_inputManager`, passes via `AppContext`
- [ ] Add `m_inputManager.Update(dt)` to main loop (before screen updates)
- [ ] Replace per-screen `Pressed*()` free functions with `m_ctx.input.Pressed*()`
  in `LibraryScreen`, `OverlayScreen`, `WiFiScreen`, `InstallerScreen`
- [ ] Remove per-screen static helper functions
- [ ] Build and verify — input behaviour unchanged

---

## Sprint 6 — Spinner + idle dim

> **Outcome:** Reusable loading spinner. Library screen dims after 60s idle.

### T6.1 — Create spinner helper

- [ ] Create `src/core/spinner.h` / `spinner.cpp`:
  ```cpp
  // Draw a 12-frame arc spinner at (x, y) with given radius and colour.
  // Frame advances automatically based on GetTime().
  void DrawSpinner(float x, float y, float radius, Color color);
  ```
- [ ] Implementation: use `GetTime()` mod 12 to select which arc segment
  to draw brighter. Draw 12 arcs in a circle, one highlighted.
- [ ] Update `CMakeLists.txt`

### T6.2 — Wire spinner into long operations

- [ ] `WiFiScreen`: show spinner during `State::Scanning` and `State::Connecting`
  states instead of static text
- [ ] `InstallerScreen`: show spinner during disk write phase
- [ ] Build and verify

### T6.3 — Implement idle screen dim

- [ ] In `LibraryScreen::Draw()`: if `m_ctx.input.IsIdleFor(60.0f)`,
  draw a semi-transparent black overlay: `DrawRectangle(0, 0, W, H, Color{0,0,0,77})`
  (77 alpha ≈ 30% dim from full brightness)
- [ ] InputManager resets idle timer on any input, so the dim lifts
  immediately when the user touches any control
- [ ] Build and verify

---

## Dependencies

```
Sprint 1 ──────► Sprint 2 ──────► Sprint 3
  │                                  │
  └──────────► Sprint 4 ◄───────────┘
                  │
                  └──► Sprint 5 ──► Sprint 6
```

- Sprint 1 must complete first (everything depends on AppContext + Theme)
- Sprints 2, 3, and 4 are independent of each other and can be done in parallel
- Sprint 5 depends on Sprint 1 (needs AppContext)
- Sprint 6 depends on Sprint 5 (needs InputManager for idle tracking)

---

## What does NOT change

- `IScreen` interface: `OnEnter`, `OnExit`, `Update(float)`, `Draw(int, int)`
- `ScreenStack` public API: `Push`, `Pop`, `Empty`, `Update`, `Draw`
- `main.cpp` — stays 6 lines
- `CMakeLists.txt` — only gets new source files, no structural changes
- All screens keep their current behaviour and layout

---

## Acceptance criteria

After all sprints are complete, the shell should:
- Build on Linux and Windows (CI green)
- Look identical to the current shell with the dark theme
- Play UI sounds on navigation, confirm, back, launch
- Animate screen push/pop with slide transitions
- Show toast notifications for WiFi connect/disconnect and install complete/fail
- Dim the library screen after 60 seconds of no input
- Show a loading spinner during WiFi scan/connect and disk install
