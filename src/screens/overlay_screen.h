// PlayOS Shell — OverlayScreen.
// Pushed on top of any screen when the Home button is pressed.
// Dims the background screen and shows a system menu.
#pragma once

#include "../screen.h"
#include "../screen_stack.h"

class OverlayScreen : public IScreen {
public:
    explicit OverlayScreen(ScreenStack& stack);

    void Update(float dt) override;
    void Draw(int W, int H) override;

private:
    ScreenStack& m_stack;
    int m_selected = 0;

    struct MenuItem { const char* label; const char* hint; };
    static constexpr MenuItem kItems[] = {
        { "WiFi Settings",  "Configure wireless networks" },
        { "Close",          "Return to previous screen"   },
    };
    static constexpr int kItemCount = 2;
};
