#include "library_screen.h"
#include "overlay_screen.h"

#include "../ui/theme.h"
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

// Scan a directory for executable games and add them to the library.
static void ScanGameDir(const fs::path& dir, std::vector<GameEntry>& games) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const auto& p = entry.path();
        // Skip non-executables and known metadata files
        const auto perms = fs::status(p, ec).permissions();
        if (ec) continue;
        const bool exec = (perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
        if (!exec) continue;
        const std::string name = p.filename().string();
        if (name.find(".so") != std::string::npos || name.find(".a") != std::string::npos || name == "CMakeLists.txt")
            continue;
        const std::string title = p.stem().string();
        games.push_back({title, "Installed game", p.string(), {}});
    }
}

static std::vector<GameEntry> BuildLibrary(const fs::path& exeDir) {
    std::vector<GameEntry> games;
    const std::string hp = FindSample(exeDir, "hello-playos");
    if (!hp.empty())
        games.push_back({"Hello PlayOS", "The reference sample — Raylib + Platform API", hp, {}});
    const std::string si = FindSample(exeDir, "space-invaders");
    if (!si.empty())
        games.push_back({"Space Invaders", "Classic arcade shooter — defend Earth!", si, {}});

    // Scan /data/games/ for user-installed titles.
    ScanGameDir("/data/games", games);

    if (games.empty())
        games.push_back({"Sleep Demo", "Sleeps 1 second and returns", "/bin/sh", {"-c", "sleep 1"}});
    return games;
}

// ── LibraryScreen ─────────────────────────────────────────────────────────

LibraryScreen::LibraryScreen(const fs::path& exeDir, AppContext& ctx)
    : m_ctx(ctx), m_library(BuildLibrary(exeDir)) {}

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
        m_ctx.stack.Push(std::make_unique<OverlayScreen>(m_ctx));

    if (!PressedConfirm()) return;

    // ── Launch game ───────────────────────────────────────────────────────
    const auto& game = m_library[m_selected];
    const int W = GetScreenWidth();
    const int H = GetScreenHeight();

    // Fade-out
    for (int f = 0; f < 30; ++f) {
        BeginDrawing();
        ClearBackground(m_ctx.theme.background);
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
        ClearBackground(m_ctx.theme.background);
        EndDrawing();
    }

    m_status = result.launched
        ? game.title + " \xe2\x80\x94 exited. Back in shell."
        : "Failed to launch: " + game.title;
}

void LibraryScreen::Draw(int W, int H) {
    ClearBackground(m_ctx.theme.background);

    // Category heading
    DrawText("LIBRARY", 160, 144, 28, m_ctx.theme.textSecondary);

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
                                 0.25f, 8, m_ctx.theme.selected);

        DrawRectangleRounded({(float)kListLeft + 24, (float)y + 20,
                              (float)kIconSz, (float)kIconSz},
                             0.2f, 6,
                             isSel ? m_ctx.theme.accent : m_ctx.theme.surfaceButton);

        DrawText(m_library[i].title.c_str(), kListLeft + kIconSz + 48,
                 y + 20, 52, isSel ? m_ctx.theme.textPrimary : m_ctx.theme.textSecondary);
        DrawText(m_library[i].subtitle.c_str(), kListLeft + kIconSz + 48,
                 y + 84, 32, m_ctx.theme.textSecondary);
    }

    // Scrollbar hint
    if (m_library.size() > 1) {
        const float thumbY = (float)(kListTop + m_selected * kRowH);
        DrawRectangle(kListLeft + kListW + 16, (int)thumbY, 4, kRowH,
                      m_ctx.theme.inactive);
    }

    // Status / help line
    if (!m_status.empty())
        DrawText(m_status.c_str(), 160, H - 144, 36, m_ctx.theme.success);
    DrawText("Navigate: D-Pad /   Launch: A   \xe2\x80\x94   Home: overlay",
             160, H - 84, 32, m_ctx.theme.textMuted);
}
