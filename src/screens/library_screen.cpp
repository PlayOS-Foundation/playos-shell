#include "library_screen.h"
#include "overlay_screen.h"

#include "raylib.h"
#include "playos/playos.h"
#include "playos/runtime/process.h"

#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

// ── helpers ──────────────────────────────────────────────────────────────

static bool PressedUp() {
    return PlayOS::Input::Pressed(PlayOS::Button::DPadUp)   || IsKeyPressed(KEY_UP);
}
static bool PressedDown() {
    return PlayOS::Input::Pressed(PlayOS::Button::DPadDown) || IsKeyPressed(KEY_DOWN);
}
static bool PressedConfirm() {
    return PlayOS::Input::Pressed(PlayOS::Button::A)        || IsKeyPressed(KEY_ENTER);
}
static bool PressedHome() {
    return PlayOS::Input::Pressed(PlayOS::Button::Home) || IsKeyPressed(KEY_H);
}

static std::string ExeName(const std::string& base) {
#ifdef _WIN32
    return base + ".exe";
#else
    return base;
#endif
}

static std::string FindSample(const fs::path& exeDir, const std::string& name) {
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

static std::vector<GameEntry> BuildLibrary(const fs::path& exeDir) {
    std::vector<GameEntry> games;
    const std::string hp = FindSample(exeDir, "hello-playos");
    if (!hp.empty())
        games.push_back({"Hello PlayOS", "The reference sample — Raylib + Platform API", hp, {}});
    const std::string si = FindSample(exeDir, "space-invaders");
    if (!si.empty())
        games.push_back({"Space Invaders", "Classic arcade shooter — defend Earth!", si, {}});
    if (games.empty())
        games.push_back({"Sleep Demo", "Sleeps 1 second and returns", "/bin/sh", {"-c", "sleep 1"}});
    return games;
}

// ── LibraryScreen ─────────────────────────────────────────────────────────

LibraryScreen::LibraryScreen(const fs::path& exeDir, ScreenStack& stack)
    : m_stack(stack), m_library(BuildLibrary(exeDir)) {}

void LibraryScreen::OnEnter() {
    // Nothing needed — keep selection where it was
}

void LibraryScreen::Update(float dt) {
    (void)dt;
    if (PressedUp())
        m_selected = (m_selected - 1 + (int)m_library.size()) % (int)m_library.size();
    if (PressedDown())
        m_selected = (m_selected + 1) % (int)m_library.size();
    if (PressedHome())
        m_stack.Push(std::make_unique<OverlayScreen>(m_stack));

    if (!PressedConfirm()) return;

    // ── Launch game ───────────────────────────────────────────────────────
    const auto& game = m_library[m_selected];
    const int W = GetScreenWidth();
    const int H = GetScreenHeight();

    // Fade-out
    for (int f = 0; f < 30; ++f) {
        BeginDrawing();
        ClearBackground(Color{8, 8, 12, 255});
        const int alpha = (int)(255.0f * ((float)f / 29.0f));
        DrawText(game.title.c_str(),
                 (W - MeasureText(game.title.c_str(), 36)) / 2,
                 H / 2 - 18, 36, Color{255, 255, 255, (unsigned char)alpha});
        EndDrawing();
    }

    const auto result = PlayOS::Runtime::LaunchAndWait(game.executable, game.args);

    // Fade-in
    for (int f = 0; f < 20; ++f) {
        BeginDrawing();
        ClearBackground(Color{8, 8, 12, 255});
        EndDrawing();
    }

    m_status = result.launched
        ? game.title + " \xe2\x80\x94 exited. Back in shell."
        : "Failed to launch: " + game.title;
}

void LibraryScreen::Draw(int W, int H) {
    ClearBackground(Color{12, 12, 18, 255});

    // Category heading
    DrawText("LIBRARY", 160, 144, 28, Color{100, 100, 140, 255});

    // ── Game list ─────────────────────────────────────────────────────────
    constexpr int kRowH    = 144;
    constexpr int kListTop = 212;
    constexpr int kListLeft = 64;
    constexpr int kListW   = 1720;
    constexpr int kIconSz  = 104;

    for (int i = 0; i < (int)m_library.size(); ++i) {
        const int y = kListTop + i * kRowH;
        const bool isSel = (i == m_selected);

        if (isSel)
            DrawRectangleRounded({(float)kListLeft, (float)y + 2,
                                  (float)kListW, (float)kRowH - 4},
                                 0.25f, 8, Color{44, 52, 68, 255});

        DrawRectangleRounded({(float)kListLeft + 24, (float)y + 20,
                              (float)kIconSz, (float)kIconSz},
                             0.2f, 6,
                             isSel ? Color{64, 130, 220, 255} : Color{32, 34, 44, 255});

        DrawText(m_library[i].title.c_str(), kListLeft + kIconSz + 48,
                 y + 20, 52, isSel ? RAYWHITE : Color{200, 200, 210, 255});
        DrawText(m_library[i].subtitle.c_str(), kListLeft + kIconSz + 48,
                 y + 84, 32, Color{120, 120, 140, 255});
    }

    // Scrollbar hint
    if (m_library.size() > 1) {
        const float thumbY = (float)(kListTop + m_selected * kRowH);
        DrawRectangle(kListLeft + kListW + 16, (int)thumbY, 4, kRowH,
                      Color{60, 60, 80, 255});
    }

    // Status / help line
    if (!m_status.empty())
        DrawText(m_status.c_str(), 160, H - 144, 36, Color{130, 150, 130, 255});
    DrawText("Navigate: D-Pad /   Launch: A   \xe2\x80\x94   Home: overlay",
             160, H - 84, 32, Color{80, 80, 100, 255});
}
