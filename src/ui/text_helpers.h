// PlayOS Shell — Text rendering helpers.
// Thin wrappers around DrawTextEx / MeasureTextEx that take a Font handle
// and fall back gracefully to the built-in Raylib font when none is loaded.
#pragma once

#include "raylib.h"

inline void DrawTextF(Font font, const char* text,
                      float x, float y, float size, Color color) {
    if (font.baseSize > 0)
        DrawTextEx(font, text, {x, y}, size, 1.0f, color);
    else
        DrawText(text, (int)x, (int)y, (int)size, color);
}

inline float MeasureTextF(Font font, const char* text, float size) {
    if (font.baseSize > 0)
        return MeasureTextEx(font, text, size, 1.0f).x;
    else
        return (float)MeasureText(text, (int)size);
}
