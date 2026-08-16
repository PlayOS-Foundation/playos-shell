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
#include "playos/playos_logging.h"

#include <stddef.h>

/* Single UI font shared by every text draw (Sprint 9). Loaded from a TTF at
 * startup; on any failure we keep texture.id == 0 and the helpers fall back
 * to Raylib's built-in default font, so text can never disappear. */
static Font s_ui_font = { 0 };

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
render_draw_triangle(float x1, float y1, float x2, float y2,
                     float x3, float y3,
                     float r, float g, float b, float a)
{
    DrawTriangle((Vector2){ x1, y1 },
                 (Vector2){ x2, y2 },
                 (Vector2){ x3, y3 },
                 color_from_rgba(r, g, b, a));
}

void
render_draw_circle(float cx, float cy, float radius,
                   float r, float g, float b, float a)
{
    DrawCircleV((Vector2){ cx, cy }, radius, color_from_rgba(r, g, b, a));
}

void
render_draw_circle_lines(float cx, float cy, float radius,
                         float r, float g, float b, float a)
{
    DrawCircleLinesV((Vector2){ cx, cy }, radius, color_from_rgba(r, g, b, a));
}

void
render_begin_scissor(int x, int y, int w, int h)
{
    BeginScissorMode(x, y, w, h);
}

void
render_end_scissor(void)
{
    EndScissorMode();
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
    Font font = s_ui_font.texture.id ? s_ui_font : GetFontDefault();

    DrawTextEx(font, text, (Vector2){ x, y },
               fontSize, spacing, color_from_rgba(r, g, b, a));
}

/* ── Gradient text ────────────────────────────────────────────────────────
 * Draws a string with the white glyph atlas recolored by a vertical gradient
 * (bottom color → top color). The gradient spans the full string using
 * gl_FragCoord, so it reads as one continuous word rather than per-glyph
 * tints. Falls back to a solid mid-tone if the shader fails to compile. */
static Shader s_gradient_shader = { 0 };
static bool   s_gradient_ready = false;
static bool   s_gradient_tried = false;
static int    s_gradient_loc_bottom = -1;
static int    s_gradient_loc_top    = -1;
static int    s_gradient_loc_range  = -1;

static const char *s_gradient_vs =
    "#version 100\n"
    "attribute vec3 vertexPosition;\n"
    "attribute vec2 vertexTexCoord;\n"
    "attribute vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *s_gradient_fs =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 gradientBottom;\n"
    "uniform vec4 gradientTop;\n"
    "uniform vec2 gradientRange;\n"
    "void main() {\n"
    "    float a = texture2D(texture0, fragTexCoord).a;\n"
    "    float t = clamp((gl_FragCoord.y - gradientRange.x) / (gradientRange.y - gradientRange.x), 0.0, 1.0);\n"
    "    vec3 c = mix(gradientBottom.rgb, gradientTop.rgb, t);\n"
    "    gl_FragColor = vec4(c, a * fragColor.a);\n"
    "}\n";

static void
gradient_shader_init(void)
{
    if (s_gradient_tried)
        return;
    s_gradient_tried = true;

    s_gradient_shader = LoadShaderFromMemory(s_gradient_vs, s_gradient_fs);
    if (s_gradient_shader.id == 0) {
        PLAYOS_LOG_W("shell", "gradient text shader failed — using solid fallback");
        return;
    }
    s_gradient_loc_bottom = GetShaderLocation(s_gradient_shader, "gradientBottom");
    s_gradient_loc_top    = GetShaderLocation(s_gradient_shader, "gradientTop");
    s_gradient_loc_range  = GetShaderLocation(s_gradient_shader, "gradientRange");
    s_gradient_ready = true;
}

void
render_draw_text_gradient(const char *text, float x, float y, float scale,
                          float bottom_r, float bottom_g, float bottom_b,
                          float top_r, float top_g, float top_b)
{
    if (!text) return;

    float fontSize = scale * 7.0f;
    float spacing  = scale;
    Font font = s_ui_font.texture.id ? s_ui_font : GetFontDefault();

    gradient_shader_init();
    if (!s_gradient_ready) {
        render_draw_text(text, x, y, scale,
                         (bottom_r + top_r) * 0.5f,
                         (bottom_g + top_g) * 0.5f,
                         (bottom_b + top_b) * 0.5f, 1.0f);
        return;
    }

    /* gl_FragCoord origin is bottom-left; raylib screen origin is top-left. */
    int   sh        = GetScreenHeight();
    float gl_bottom = (float)sh - (y + fontSize);
    float gl_top    = (float)sh - y;
    float bottom[4] = { bottom_r, bottom_g, bottom_b, 1.0f };
    float top[4]    = { top_r, top_g, top_b, 1.0f };
    float range[2]  = { gl_bottom, gl_top };

    SetShaderValue(s_gradient_shader, s_gradient_loc_bottom, bottom, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_gradient_shader, s_gradient_loc_top,    top,    SHADER_UNIFORM_VEC4);
    SetShaderValue(s_gradient_shader, s_gradient_loc_range,  range,  SHADER_UNIFORM_VEC2);

    BeginShaderMode(s_gradient_shader);
    DrawTextEx(font, text, (Vector2){ x, y }, fontSize, spacing, WHITE);
    EndShaderMode();
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

    Font font = s_ui_font.texture.id ? s_ui_font : GetFontDefault();
    Vector2 size = MeasureTextEx(font, text, scale * 7.0f, scale);
    return size.x;
}

void
render_font_init(void)
{
    /* Resolved relative to the CWD when run from the repo tree, and via the
     * absolute install path on the Ally image. First hit wins; otherwise
     * Raylib's default font remains in effect. */
    static const char *candidates[] = {
        "/usr/share/playos-shell/assets/Silkscreen-Regular.ttf",
        "assets/Silkscreen-Regular.ttf",
        "Silkscreen-Regular.ttf",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (FileExists(candidates[i])) {
            s_ui_font = LoadFontEx(candidates[i], 32, NULL, 95);
            PLAYOS_LOG_I("shell", "loaded UI font: %s",
                         candidates[i]);
            return;
        }
    }

    PLAYOS_LOG_W("shell", "UI font not found — using raylib default");
}
