#include "shell_app.h"
#include "screens/library_screen.h"

#include "raylib.h"
#include "playos/playos.h"

#include <cstdlib>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

int ShellApp::Run(int argc, char** argv) {
    const fs::path exeDir =
        fs::absolute(fs::path(argc > 0 ? argv[0] : "")).parent_path();

    // Fullscreen on runtime devices; set PLAYOS_WINDOWED=1 for dev.
    const bool windowed = (std::getenv("PLAYOS_WINDOWED") != nullptr);
    auto displayInfo = PlayOS::Display::Current();
    if (!windowed) {
        SetConfigFlags(FLAG_FULLSCREEN_MODE);
        InitWindow(0, 0, "PlayOS Shell");
    } else {
        const int W0 = displayInfo.width  > 0 ? displayInfo.width  : 1280;
        const int H0 = displayInfo.height > 0 ? displayInfo.height : 720;
        InitWindow(W0, H0, "PlayOS Shell");
    }
    const int W = GetScreenWidth();
    const int H = GetScreenHeight();
    SetTargetFPS(displayInfo.refreshRate > 0 ? displayInfo.refreshRate : 60);
    SetExitKey(0);   // disable default ESC→close; overlay handles ESC as "back"
    HideCursor();

    PlayOS::Lifecycle::Init();
    m_icons.Load();

    // Load device profile (RFC-0006) — shows device name in status bar.
    // Profile path from env var or default.
    const char* profileId = std::getenv("PLAYOS_PROFILE");
    std::string profilePath;
    if (profileId && profileId[0])
        profilePath = std::string("/etc/playos/device-profiles/") + profileId + ".toml";
    else
        profilePath = "/etc/playos/device-profiles/default.toml";
    if (auto p = PlayOS::DeviceProfile::Load(profilePath))
        m_statusBar.SetDeviceName(p->device().name);

    // Push the default screen
    m_stack.Push(std::make_unique<LibraryScreen>(exeDir, m_stack));

    while (!WindowShouldClose() && !m_stack.Empty()) {
        const float dt = GetFrameTime();
        PlayOS::Lifecycle::Update();

        m_statusBar.Update(dt);
        m_stack.Update(dt);

        BeginDrawing();
        m_stack.Draw(W, H);
        m_statusBar.Draw(W, H);
        EndDrawing();
    }

    PlayOS::Lifecycle::Shutdown();
    m_icons.Unload();
    CloseWindow();
    return 0;
}
