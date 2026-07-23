#include "theme.h"

Theme Theme::Dark() {
    Theme t;

    // Backgrounds
    t.background    = Color{12, 12, 18, 255};
    t.surface       = Color{18, 18, 28, 255};
    t.surfaceInput  = Color{30, 30, 50, 255};
    t.surfaceButton = Color{25, 25, 40, 255};
    t.statusBarBg   = Color{8, 8, 14, 180};

    // Borders & accents
    t.border    = Color{60, 60, 100, 255};
    t.separator = Color{40, 40, 60, 255};
    t.accent    = Color{64, 130, 220, 255};
    t.selected  = Color{44, 52, 68, 255};

    // Text
    t.textPrimary   = RAYWHITE;
    t.textSecondary = Color{160, 160, 180, 255};
    t.textMuted     = Color{80, 80, 100, 255};

    // Semantic
    t.success  = Color{80, 200, 80, 255};
    t.warning  = Color{220, 180, 40, 255};
    t.danger   = Color{220, 80, 80, 255};
    t.info     = Color{100, 160, 255, 255};
    t.inactive = Color{60, 60, 80, 255};

    // Overlay
    t.overlayDim = Color{0, 0, 0, 180};

    return t;
}
