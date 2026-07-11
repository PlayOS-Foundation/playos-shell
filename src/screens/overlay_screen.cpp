#include "overlay_screen.h"

#include "raylib.h"
#include "playos/playos.h"

static bool OverlayBack() {
    return PlayOS::Input::Pressed(PlayOS::Button::B)    || IsKeyPressed(KEY_ESCAPE)
        || PlayOS::Input::Pressed(PlayOS::Button::Home);
}

OverlayScreen::OverlayScreen(ScreenStack& stack) : m_stack(stack) {}

void OverlayScreen::Update(float dt) {
    (void)dt;
    if (OverlayBack())
        m_stack.Pop();
}

void OverlayScreen::Draw(int W, int H) {
    // Dim
    DrawRectangle(0, 0, W, H, Color{0, 0, 0, 160});

    const char* title = "SYSTEM";
    const int tw = MeasureText(title, 64);
    DrawText(title, (W - tw) / 2, H / 2 - 160, 64, Color{180, 180, 200, 255});

    DrawText("Press B or Home to close",
             (W - 480) / 2, H / 2 + 80, 44, Color{140, 140, 160, 255});

    const char* savePath = PlayOS::Storage::SavePath();
    const char* cfgPath  = PlayOS::Storage::ConfigPath();
    DrawText(TextFormat("Saves:  %s", savePath), 80, H - 160, 28, Color{100, 100, 130, 255});
    DrawText(TextFormat("Config: %s", cfgPath),  80, H - 116, 28, Color{100, 100, 130, 255});
}
