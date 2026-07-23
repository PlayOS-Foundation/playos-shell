#include "icons.h"

static const int kCPs[] = { 0xF2C0, 0xEACC, 0xEAB0, 0xEAAE };

void Icons::Load() {
    const char* path = "/usr/share/playos/fonts/remixicon.ttf";
    if (!FileExists(path)) return;
    m_font = LoadFontEx(path, 52, const_cast<int*>(kCPs), 4);
}

void Icons::Unload() {
    if (IsLoaded()) UnloadFont(m_font);
    m_font = {};
}

void Icons::Draw(const char* glyph, const char* fallback,
                 float x, float y, float size, Color color) const {
    if (IsLoaded())
        DrawTextEx(m_font, glyph, {x, y}, size, 0, color);
    else
        DrawText(fallback, (int)x, (int)y, (int)size, color);
}

float Icons::Measure(const char* glyph, const char* fallback, float size) const {
    if (IsLoaded()) {
        Vector2 v = MeasureTextEx(m_font, glyph, size, 0);
        return v.x;
    }
    return (float)MeasureText(fallback, (int)size);
}
