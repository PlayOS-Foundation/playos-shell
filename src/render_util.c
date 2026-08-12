/**
 * render_util.c — thin rendering wrappers over the Raylib 6.0 draw API
 *
 * The shell no longer owns GLES2 state: all drawing is delegated to Raylib
 * through these one-line helpers so the screen_* modules keep their existing
 * render_* call sites. Text uses Raylib's built-in default font (the retired
 * 5×7 bitmap font and raw GLSL shaders are gone).
 *
 * SPDX-License-Identifier: MIT
 */

#include "shell.h"
#include "raylib.h"

#include <stddef.h>

/* Convert the shell's normalized RGBA (0.0–1.0) convention to a raylib Color */
static Color
color_from_rgba(float r, float g, float b, float a)
{
    return (Color){
        (unsigned char)(r * 255.0f),
        (unsigned char)(g * 255.0f),
        (unsigned char)(b * 255.0f),
        (unsigned char)(a * 255.0f)
    };
}

void
render_begin_frame(float r, float g, float b, float a)
{
    BeginDrawing();
    ClearBackground(color_from_rgba(r, g, b, a));
}

void
render_end_frame(struct playos_shell *s)
{
    (void)s;
    EndDrawing();
}

void
render_draw_rect(float x, float y, float w, float h,
                 float r, float g, float b, float a)
{
    DrawRectangleRec((Rectangle){ x, y, w, h }, color_from_rgba(r, g, b, a));
}

void
render_draw_text(const char *text, float x, float y,
                 float scale, float r, float g, float b, float a)
{
    if (!text) return;

    /* One retired-glyph pixel == one raylib font pixel: the old 5×7 glyph
     * was 7 rows tall and had 1px spacing, so fontSize = 7*scale and
     * spacing = scale keeps line height and centering proportional. */
    float fontSize = scale * 7.0f;
    float spacing  = scale;

    DrawTextEx(GetFontDefault(), text, (Vector2){ x, y },
               fontSize, spacing, color_from_rgba(r, g, b, a));
}

void
render_screen_dims(int *w, int *h)
{
    if (w) *w = GetScreenWidth();
    if (h) *h = GetScreenHeight();
}

float
render_text_width(const char *text, float scale)
{
    if (!text) return 0.0f;

    Vector2 size = MeasureTextEx(GetFontDefault(), text,
                                 scale * 7.0f, scale);
    return size.x;
}
