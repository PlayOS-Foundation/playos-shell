#include "shell_app.h"
#include "screens/library_screen.h"

#include "raylib.h"
#include "playos/playos.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;

int ShellApp::Run(int argc, char** argv) {
    const fs::path exeDir =
        fs::absolute(fs::path(argc > 0 ? argv[0] : "")).parent_path();

    TraceLog(LOG_INFO, "PLAYOS-SHELL: Starting...");

    // Fullscreen on runtime devices; set PLAYOS_WINDOWED=1 for dev.
    const bool windowed = (std::getenv("PLAYOS_WINDOWED") != nullptr);
    auto displayInfo = PlayOS::Display::Current();
    TraceLog(LOG_INFO, "PLAYOS-SHELL: Display: %dx%d @ %dHz, windowed=%d",
             displayInfo.width, displayInfo.height, displayInfo.refreshRate, windowed);

    if (!windowed) {
        TraceLog(LOG_INFO, "PLAYOS-SHELL: Fullscreen mode, calling InitWindow(0,0)...");
        SetConfigFlags(FLAG_FULLSCREEN_MODE);
        InitWindow(0, 0, "PlayOS Shell");
    } else {
        const int W0 = displayInfo.width  > 0 ? displayInfo.width  : 1280;
        const int H0 = displayInfo.height > 0 ? displayInfo.height : 720;
        TraceLog(LOG_INFO, "PLAYOS-SHELL: Windowed mode %dx%d", W0, H0);
        InitWindow(W0, H0, "PlayOS Shell");
    }
    const int W = GetScreenWidth();
    const int H = GetScreenHeight();
    TraceLog(LOG_INFO, "PLAYOS-SHELL: Window created: %dx%d", W, H);
    TraceLog(LOG_INFO, "PLAYOS-SHELL: IsWindowReady=%d, IsWindowFullscreen=%d",
             IsWindowReady(), IsWindowFullscreen());
    SetTargetFPS(displayInfo.refreshRate > 0 ? displayInfo.refreshRate : 60);
    TraceLog(LOG_INFO, "PLAYOS-SHELL: SetTargetFPS done");
    SetExitKey(0);   // disable default ESC→close; overlay handles ESC as "back"
    TraceLog(LOG_INFO, "PLAYOS-SHELL: SetExitKey done");
    HideCursor();
    TraceLog(LOG_INFO, "PLAYOS-SHELL: HideCursor done");

    TraceLog(LOG_INFO, "PLAYOS-SHELL: Calling Lifecycle::Init...");
    PlayOS::Lifecycle::Init();
    TraceLog(LOG_INFO, "PLAYOS-SHELL: Lifecycle::Init done");
    m_icons.Load();
    TraceLog(LOG_INFO, "PLAYOS-SHELL: Icons loaded");

    TraceLog(LOG_INFO, "PLAYOS-SHELL: Loading device profile...");
    std::string profilePath;
    const char* profileId = std::getenv("PLAYOS_PROFILE");
    if (profileId && profileId[0]) {
        profilePath = std::string("/etc/playos/device-profiles/") + profileId + ".toml";
    } else {
        std::ifstream dmi("/sys/class/dmi/id/product_name");
        if (dmi.is_open()) {
            std::string product;
            std::getline(dmi, product);
            if (product.find("ROG Ally") != std::string::npos)
                profilePath = "/etc/playos/device-profiles/rog-ally.toml";
            else if (product.find("Steam Deck") != std::string::npos)
                profilePath = "/etc/playos/device-profiles/steam-deck.toml";
            else if (product.find("Legion Go") != std::string::npos)
                profilePath = "/etc/playos/device-profiles/legion-go.toml";
        }
        if (profilePath.empty())
            profilePath = "/etc/playos/device-profiles/default.toml";
    }
    if (auto p = PlayOS::DeviceProfile::Load(profilePath)) {
        m_statusBar.SetDeviceName(p->device().name);
        TraceLog(LOG_INFO, "PLAYOS-SHELL: Device profile loaded: %s", p->device().name.c_str());
    } else {
        TraceLog(LOG_INFO, "PLAYOS-SHELL: Device profile not found, using defaults");
    }

    // Push the default screen
    TraceLog(LOG_INFO, "PLAYOS-SHELL: Creating LibraryScreen...");
    m_stack.Push(std::make_unique<LibraryScreen>(exeDir, m_stack));
    TraceLog(LOG_INFO, "PLAYOS-SHELL: LibraryScreen created, entering main loop");

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
