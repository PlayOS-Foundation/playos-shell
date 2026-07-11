// PlayOS Shell — reference console UI (vertical slice).
//
// Proves the console loop from playos-spec:
//   Shell -> select game -> launch -> play -> return to Shell
//
// Rendering: Raylib (the reference engine).
// Platform services: the PlayOS Platform API (input, lifecycle).
// Launching: the PlayOS Runtime (LaunchAndWait).

#include "raylib.h"

#include "playos/playos.h"
#include "playos/runtime/process.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#ifdef __linux__
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <cstring>
#include <net/if.h>
#endif

namespace {

namespace fs = std::filesystem;

// ── helpers ──────────────────────────────────────────────────────────────

std::string ExeName(const std::string& base) {
#ifdef _WIN32
    return base + ".exe";
#else
    return base;
#endif
}

// ── game library ─────────────────────────────────────────────────────────

struct GameEntry {
    std::string title;
    std::string subtitle;    // short description shown below the title
    std::string executable;
    std::vector<std::string> args;
};

std::string FindSample(const fs::path& exeDir, const std::string& name) {
    const std::string exe = ExeName(name);
    for (const char* buildDir : {"build", "build-linux"}) {
        const fs::path candidate =
            exeDir / ".." / ".." / "playos-samples" / buildDir / exe;
        std::error_code ec;
        const fs::path resolved = fs::weakly_canonical(candidate, ec);
        if (!ec && fs::exists(resolved)) return resolved.string();
    }
    return {};
}

std::vector<GameEntry> DemoLibrary(const fs::path& exeDir) {
    std::vector<GameEntry> games;
    const std::string sample = FindSample(exeDir, "hello-playos");
    if (!sample.empty()) {
        games.push_back({"Hello PlayOS",
                         "The reference sample — Raylib + Platform API",
                         sample, {}});
    }
    const std::string invaders = FindSample(exeDir, "space-invaders");
    if (!invaders.empty()) {
        games.push_back({"Space Invaders",
                         "Classic arcade shooter — defend Earth!",
                         invaders, {}});
    }
#ifdef _WIN32
    games.push_back({"Notepad", "Windows text editor (demo)",
                     "C:\\Windows\\System32\\notepad.exe", {}});
    games.push_back({"Terminal Echo", "Prints a message and waits",
                     "C:\\Windows\\System32\\cmd.exe",
                     {"/c", "echo PlayOS launched me && pause"}});
#else
    games.push_back({"Sleep Demo", "Sleeps 1 second and returns",
                     "/bin/sh", {"-c", "sleep 1"}});
#endif
    return games;
}

// ── input helpers ────────────────────────────────────────────────────────
//
// All input goes through the PlayOS Platform API. The linked input backend
// (playos-input-raylib for the PoC) handles gamepad mapping and keyboard
// fallback.

bool PressedUp() {
    return PlayOS::Input::Pressed(PlayOS::Button::DPadUp) ||
           IsKeyPressed(KEY_UP);
}
bool PressedDown() {
    return PlayOS::Input::Pressed(PlayOS::Button::DPadDown) ||
           IsKeyPressed(KEY_DOWN);
}
bool PressedConfirm() {
    return PlayOS::Input::Pressed(PlayOS::Button::A) ||
           IsKeyPressed(KEY_ENTER);
}
bool PressedBack() {
    return PlayOS::Input::Pressed(PlayOS::Button::B) ||
           IsKeyPressed(KEY_ESCAPE);
}
bool PressedHome() {
    return PlayOS::Input::Pressed(PlayOS::Button::Home);
}

// ── animation helper ─────────────────────────────────────────────────────

// Smoothly moves `value` toward `target` at `speed` units/sec. Call each
// frame with GetFrameTime().
float Track(float value, float target, float speed, float dt) {
    if (fabsf(target - value) < 0.5f) return target;
    return value + (target - value) * std::min(speed * dt, 1.0f);
}

// ── network helpers ──────────────────────────────────────────────────────

// Returns the primary non-loopback IPv4 address, or empty string if none.
// Cached after first call — IP rarely changes while the shell is running.
std::string GetPrimaryIP() {
    static std::string cached;
    static bool tried = false;
    if (tried) return cached;
    tried = true;

#ifdef __linux__
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) return "";

    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        // Skip loopback
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        // Skip interfaces that are down
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        char ip[INET_ADDRSTRLEN];
        void *addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
        inet_ntop(AF_INET, addr, ip, sizeof(ip));
        cached = ip;
        break;
    }
    freeifaddrs(ifaddr);
#endif
    return cached;
}

} // namespace

// ── main ─────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    const fs::path exeDir =
        fs::absolute(fs::path(argc > 0 ? argv[0] : "")).parent_path();

    // Fullscreen on Linux (console experience), windowed on Windows
    // (convenient for development). On a runtime device the compositor
    // fullscreens every Wayland client surface regardless.
    auto displayInfo = PlayOS::Display::Current();
#ifdef __linux__
    SetConfigFlags(FLAG_FULLSCREEN_MODE);
    InitWindow(0, 0, "PlayOS Shell");
    const int W = GetScreenWidth();
    const int H = GetScreenHeight();
#else
    const int W = displayInfo.width > 0 ? displayInfo.width : 1280;
    const int H = displayInfo.height > 0 ? displayInfo.height : 720;
    InitWindow(W, H, "PlayOS Shell");
#endif
    SetTargetFPS(displayInfo.refreshRate > 0 ? displayInfo.refreshRate : 60);
    HideCursor();

    PlayOS::Lifecycle::Init();

    const auto library = DemoLibrary(exeDir);
    int selected = 0;
    std::string status;
    bool showOverlay = false;   // Home toggles a simple overlay

    // ── main loop ────────────────────────────────────────────────────────

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        PlayOS::Lifecycle::Update();

        // ── input ────────────────────────────────────────────────────────

        if (!showOverlay) {
            if (PressedUp()) {
                selected = (selected - 1 + (int)library.size()) %
                           (int)library.size();
            }
            if (PressedDown()) {
                selected = (selected + 1) % (int)library.size();
            }
            if (PressedHome()) {
                showOverlay = true;
            }
        } else {
            // Overlay active: B or Home dismisses it.
            if (PressedBack() || PressedHome()) {
                showOverlay = false;
            }
        }

        const bool launchRequested = !showOverlay && PressedConfirm();

        // ── draw ─────────────────────────────────────────────────────────

        BeginDrawing();
        ClearBackground(Color{12, 12, 18, 255});

        // Status bar
        DrawRectangle(0, 0, W, 72, Color{8, 8, 14, 180});
        DrawText("PlayOS", 32, 16, 40, Color{180, 180, 200, 255});

        // Controller indicator
        if (PlayOS::Input::ControllerConnected()) {
            DrawCircle(W - 200, 36, 10, Color{60, 200, 80, 255});
            DrawText("Controller", W - 176, 16, 32, Color{140, 160, 150, 255});
        } else {
            DrawText("No controller", W - 260, 16, 32,
                     Color{100, 100, 110, 255});
        }

        // Battery indicator (top-right, only when capability present)
        if (PlayOS::Capabilities::Has(PlayOS::Capability::Battery)) {
            const float batt = PlayOS::Battery::Level();
            const bool charging = PlayOS::Battery::IsCharging();
            if (batt >= 0.0f) {
                const int pct = (int)(batt * 100.0f + 0.5f);
                Color battCol = batt > 0.3f ? Color{80, 200, 80, 255}
                              : batt > 0.1f ? Color{220, 180, 40, 255}
                                            : Color{220, 60, 60, 255};
                const char* battLabel = charging
                    ? TextFormat("~%d%%", pct)
                    : TextFormat("%d%%", pct);
                const int bw = MeasureText(battLabel, 28);
                DrawText(battLabel, W - bw - 20, H - 56, 28, battCol);
            }
        }

        // Category heading
        DrawText("LIBRARY", 160, 144, 28, Color{100, 100, 140, 255});

        // ── game list ────────────────────────────────────────────────────

        constexpr int kRowH = 144;
        constexpr int kListTop = 212;
        constexpr int kListLeft = 64;
        constexpr int kListW = 1720;
        constexpr int kIconSz = 104;

        for (int i = 0; i < (int)library.size(); ++i) {
            const int y = kListTop + i * kRowH;
            const bool isSel = (i == selected);

            if (isSel) {
                // Highlight bar
                DrawRectangleRounded(
                    Rectangle{(float)kListLeft, (float)y + 2, (float)kListW,
                              (float)kRowH - 4},
                    0.25f, 8, Color{44, 52, 68, 255});
            }

            // Icon placeholder
            DrawRectangleRounded(
                Rectangle{(float)kListLeft + 24, (float)y + 20, (float)kIconSz,
                          (float)kIconSz},
                0.2f, 6,
                isSel ? Color{64, 130, 220, 255} : Color{32, 34, 44, 255});

            // Title
            DrawText(library[i].title.c_str(), kListLeft + kIconSz + 48,
                     y + 20, 52,
                     isSel ? RAYWHITE : Color{200, 200, 210, 255});

            // Subtitle
            DrawText(library[i].subtitle.c_str(), kListLeft + kIconSz + 48,
                     y + 84, 32, Color{120, 120, 140, 255});
        }

        // Scrollbar hint
        if (library.size() > 1) {
            const float thumbH =
                (float)(kRowH * library.size()) / (float)library.size();
            const float thumbY = kListTop + selected * kRowH;
            DrawRectangle(kListLeft + kListW + 16, (int)thumbY, 4, kRowH,
                          Color{60, 60, 80, 255});
        }

        // ── status / help line ───────────────────────────────────────────

        if (!status.empty()) {
            DrawText(status.c_str(), 160, H - 144, 36,
                     Color{130, 150, 130, 255});
        }
        DrawText("Navigate: D-Pad /   Launch: A   —   Home: overlay",
                 160, H - 84, 32, Color{80, 80, 100, 255});

        // ── IP address (bottom-right) ───────────────────────────────────

        const std::string ip = GetPrimaryIP();
        if (!ip.empty()) {
            const char *label = TextFormat("IP: %s", ip.c_str());
            const int labelW = MeasureText(label, 32);
            DrawText(label, W - labelW - 40, H - 136, 32,
                     Color{100, 180, 100, 255});
        }

        // ── overlay ──────────────────────────────────────────────────────

        if (showOverlay) {
            // Dim background
            DrawRectangle(0, 0, W, H, Color{0, 0, 0, 160});

            const char* overlayTitle = "SYSTEM";
            const int titleW = MeasureText(overlayTitle, 64);
            DrawText(overlayTitle, (W - titleW) / 2, H / 2 - 160, 64,
                     Color{180, 180, 200, 255});

            DrawText("Press B or Home to close", (W - 480) / 2, H / 2 + 80,
                     44, Color{140, 140, 160, 255});

            // Storage info (proves storage backend is wired)
            const char* savePath = PlayOS::Storage::SavePath();
            const char* cfgPath  = PlayOS::Storage::ConfigPath();
            DrawText(TextFormat("Saves: %s", savePath),
                     80, H - 160, 28, Color{100, 100, 130, 255});
            DrawText(TextFormat("Config: %s", cfgPath),
                     80, H - 116, 28, Color{100, 100, 130, 255});
        }

        EndDrawing();

        // ── launch (blocking) ────────────────────────────────────────────

        if (launchRequested) {
            const auto& game = library[selected];

#ifdef __linux__
            // Exit fullscreen so the launched game window is visible on top.
            ToggleFullscreen();
#endif

            // Fade-out transition (~0.5 s)
            for (int f = 0; f < 30; ++f) {
                BeginDrawing();
                ClearBackground(Color{8, 8, 12, 255});
                const int alpha = (int)(255.0f * ((float)f / 29.0f));
                DrawText(game.title.c_str(), (W - MeasureText(game.title.c_str(), 36)) / 2,
                         H / 2 - 18, 36, Color{255, 255, 255, (unsigned char)alpha});
                EndDrawing();
            }

            const auto result =
                PlayOS::Runtime::LaunchAndWait(game.executable, game.args);

#ifdef __linux__
            // Restore fullscreen after the game exits.
            ToggleFullscreen();
#endif

            // Fade-in transition
            for (int f = 0; f < 20; ++f) {
                BeginDrawing();
                ClearBackground(Color{8, 8, 12, 255});
                EndDrawing();
            }

            if (!result.launched) {
                status = "Failed to launch: " + game.title;
            } else {
                status = game.title + " — exited. Back in shell.";
            }
        }
    }

    PlayOS::Lifecycle::Shutdown();
    CloseWindow();
    return 0;
}
