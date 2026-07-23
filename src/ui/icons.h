// PlayOS Shell — Icon rendering helpers.
// Loads the Remixicon TTF font (if present) and provides DrawIcon().
// Falls back to ASCII letters gracefully when the font is absent.
#pragma once

#include "raylib.h"

class Icons {
public:
    // Load from /usr/share/playos/fonts/remixicon.ttf.
    // Safe to call even if the file is absent — IsLoaded() returns false.
    void Load();
    void Unload();

    bool IsLoaded() const { return m_font.baseSize > 0; }

    // Draw a Remixicon glyph at (x,y) with the given size and colour.
    // If the font is not loaded, draws the fallback ASCII letter instead.
    void Draw(const char* glyph, const char* fallback,
              float x, float y, float size, Color color) const;

    // Measure the width of a glyph (or fallback) at the given size.
    float Measure(const char* glyph, const char* fallback, float size) const;

    // Pre-encoded UTF-8 glyph strings for the codepoints we use.
    static constexpr const char* Wifi      = "\xEF\x8B\x80"; // U+F2C0
    static constexpr const char* Bluetooth = "\xEA\xB3\x8C"; // U+EACC
    static constexpr const char* Battery   = "\xEA\xAA\xB0"; // U+EAB0
    static constexpr const char* BatteryCharge = "\xEA\xAA\xAE"; // U+EAAE

private:
    Font m_font = {};
};
