#include "overlay_screen.h"
#include "wifi_screen.h"
#include "installer_screen.h"

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

OverlayScreen::OverlayScreen(ScreenStack& stack) : m_stack(stack) {}

void OverlayScreen::Update(float dt) {
    (void)dt;
    if (OvBack())    { m_stack.Pop(); return; }
    if (OvUp())      m_selected = std::max(0, m_selected - 1);
    if (OvDown())    m_selected = std::min(kItemCount - 1, m_selected + 1);
    if (OvConfirm()) {
        switch (m_selected) {
        case 0: m_stack.Push(std::make_unique<WiFiScreen>(m_stack)); break;
        case 1: m_stack.Push(std::make_unique<InstallerScreen>(m_stack)); break;
        case 2: m_stack.Pop(); break;
        }
    }
}

void OverlayScreen::Draw(int W, int H) {
    // Dim
    DrawRectangle(0, 0, W, H, Color{0, 0, 0, 180});

    // Panel
    const int panW = 700, panH = 500;
    const int px = (W - panW) / 2, py = (H - panH) / 2;
    DrawRectangleRounded({(float)px, (float)py, (float)panW, (float)panH},
                         0.1f, 12, Color{18, 18, 28, 255});
    DrawRectangleRoundedLines({(float)px, (float)py, (float)panW, (float)panH},
                               0.1f, 12, 2.0f, Color{60, 60, 100, 255});

    // Title
    DrawText("SYSTEM", px + 40, py + 36, 52, Color{180, 180, 220, 255});
    DrawRectangle(px + 40, py + 100, panW - 80, 2, Color{40, 40, 70, 255});

    // Menu items
    for (int i = 0; i < kItemCount; ++i) {
        const int iy = py + 130 + i * 110;
        const bool sel = (i == m_selected);
        if (sel)
            DrawRectangleRounded({(float)(px + 30), (float)(iy - 8),
                                   (float)(panW - 60), 90.0f},
                                  0.2f, 8, Color{44, 52, 80, 255});
        DrawText(kItems[i].label, px + 60, iy + 4, 36,
                 sel ? RAYWHITE : Color{160, 160, 180, 255});
        DrawText(kItems[i].hint, px + 60, iy + 48, 22,
                 Color{80, 80, 100, 255});
    }

    // Help
    DrawText("[A] Select    [B] / [Home] Close",
             px + 40, py + panH - 54, 24, Color{80, 80, 100, 255});

    // Storage info (bottom)
    DrawText(TextFormat("Saves: %s",  PlayOS::Storage::SavePath()),
             80, H - 160, 24, Color{60, 60, 90, 255});
    DrawText(TextFormat("Config: %s", PlayOS::Storage::ConfigPath()),
             80, H - 124, 24, Color{60, 60, 90, 255});
}
