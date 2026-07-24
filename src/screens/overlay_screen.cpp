#include "overlay_screen.h"
#include "wifi_screen.h"
#include "installer_screen.h"

#include "../ui/theme.h"
#include "raylib.h"
#include "playos/playos.h"
#include <cstdlib>
#include <memory>

constexpr OverlayScreen::MenuItem OverlayScreen::kItems[];

static bool OvUp()      { return PlayOS::Input::Pressed(PlayOS::Button::DPadUp)   || IsKeyPressed(KEY_UP);    }
static bool OvDown()    { return PlayOS::Input::Pressed(PlayOS::Button::DPadDown) || IsKeyPressed(KEY_DOWN);  }
static bool OvConfirm() { return PlayOS::Input::Pressed(PlayOS::Button::A)        || IsKeyPressed(KEY_ENTER); }
static bool OvBack()    { return PlayOS::Input::Pressed(PlayOS::Button::B)        || IsKeyPressed(KEY_ESCAPE)
                              || PlayOS::Input::Pressed(PlayOS::Button::Home); }

OverlayScreen::OverlayScreen(AppContext& ctx) : m_ctx(ctx) {}

void OverlayScreen::Update(float dt) {
    (void)dt;
    if (OvBack())    { m_ctx.stack.Pop(); return; }
    if (OvUp())      m_selected = std::max(0, m_selected - 1);
    if (OvDown())    m_selected = std::min(kItemCount - 1, m_selected + 1);
    if (OvConfirm()) {
        switch (m_selected) {
        case 0: m_ctx.stack.Push(std::make_unique<WiFiScreen>(m_ctx)); break;
        case 1: m_ctx.stack.Push(std::make_unique<InstallerScreen>(m_ctx)); break;
        case 2: m_ctx.stack.Pop(); break;
        }
    }
}

void OverlayScreen::Draw(int W, int H) {
    // Dim
    DrawRectangle(0, 0, W, H, m_ctx.theme.overlayDim);

    // Panel
    const int panW = 700, panH = 500;
    const int px = (W - panW) / 2, py = (H - panH) / 2;
    DrawRectangleRounded({(float)px, (float)py, (float)panW, (float)panH},
                         0.1f, 12, m_ctx.theme.surface);
    DrawRectangleRoundedLines({(float)px, (float)py, (float)panW, (float)panH},
                               0.1f, 12, 1.0f, m_ctx.theme.border);

    // Title
    DrawText("SYSTEM", px + 40, py + 36, 52, m_ctx.theme.textPrimary);
    DrawRectangle(px + 40, py + 100, panW - 80, 2, m_ctx.theme.separator);

    // Menu items
    for (int i = 0; i < kItemCount; ++i) {
        const int iy = py + 130 + i * 110;
        const bool sel = (i == m_selected);
        if (sel)
            DrawRectangleRounded({(float)(px + 30), (float)(iy - 8),
                                   (float)(panW - 60), 90.0f},
                                  0.2f, 8, m_ctx.theme.selected);
        DrawText(kItems[i].label, px + 60, iy + 4, 36,
                 sel ? m_ctx.theme.textPrimary : m_ctx.theme.textSecondary);
        DrawText(kItems[i].hint, px + 60, iy + 48, 22,
                 m_ctx.theme.textMuted);
    }

    // Help
    DrawText("[A] Select    [B] / [Home] Close",
             px + 40, py + panH - 54, 24, m_ctx.theme.textMuted);

    // Storage info (bottom)
    DrawText(TextFormat("Saves: %s",  PlayOS::Storage::SavePath()),
             80, H - 160, 24, m_ctx.theme.textMuted);
    DrawText(TextFormat("Config: %s", PlayOS::Storage::ConfigPath()),
             80, H - 124, 24, m_ctx.theme.textMuted);
}
