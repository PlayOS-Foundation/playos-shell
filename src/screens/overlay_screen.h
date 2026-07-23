// PlayOS Shell — OverlayScreen.
// Pushed on top of any screen when the Home button is pressed.
// Dims the background screen and shows a system menu.
#pragma once

#include "../core/screen.h"
#include "../core/app_context.h"

class OverlayScreen : public IScreen {
public:
    explicit OverlayScreen(AppContext& ctx);

    void Update(float dt) override;
    void Draw(int W, int H) override;

private:
    AppContext& m_ctx;
    int m_selected = 0;

    struct MenuItem { const char* label; const char* hint; };
    static constexpr MenuItem kItems[] = {
        { "WiFi Settings",     "Configure wireless networks"         },
        { "Install to Disk",   "Install PlayOS to the internal disk" },
        { "Close",             "Return to previous screen"           },
    };
    static constexpr int kItemCount = 3;
};
