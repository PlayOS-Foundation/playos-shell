/**
 * screen_home.c — Home screen for PlayOS Shell
 *
 * Animated PlayOS-branded home screen with logo, menu shortcuts,
 * and system info footer.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shell.h"
#include "playos/playos_system.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── Enter ─────────────────────────────────────────────────────────────── */

void
screen_home_enter(struct playos_shell *s)
{
    s->home_cursor = 0;
}

/* ── Update ────────────────────────────────────────────────────────────── */

void
screen_home_update(struct playos_shell *s)
{
    /* D-pad up/down: move cursor between Library (0) and Settings (1) */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP)) {
        if (s->home_cursor > 0)
            s->home_cursor--;
    }
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN)) {
        if (s->home_cursor < 1)
            s->home_cursor++;
    }

    /* A: launch the selected menu item */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
        if (s->home_cursor == 1) {
            s->current_screen = SCREEN_SETTINGS;
            screen_settings_enter(s);
        } else {
            s->current_screen = SCREEN_LIBRARY;
            screen_library_enter(s);
        }
        return;
    }
}

/* ── Draw ──────────────────────────────────────────────────────────────── */

void
screen_home_draw(struct playos_shell *s)
{
    int w = s->output_width;
    int h = s->output_height;

    /* ── PlayOS blue background ── */
    render_begin_frame(0.08f, 0.16f, 0.30f, 1.0f);

    /* ── Animated blue wave (PSP/PS3 media-bar style) ──
     * Three overlapping sine ribbons flowing left-to-right, in shades of
     * blue, fading with depth. Each ribbon is a triangle strip between its
     * top edge (a travelling sine) and a parallel bottom edge. */
    {
        float wave_base  = (float)h * 0.58f;
        float wave_amp   = (float)h * 0.11f;
        float wave_freq  = 0.0065f;   /* radians per pixel */
        float wave_speed = 1.8f;      /* phase radians per second */
        int   steps      = 96;
        float dx         = (float)w / (float)steps;

        for (int layer = 0; layer < 3; layer++) {
            float phase  = (float)s->elapsed_time * wave_speed
                           + (float)layer * 1.9f;
            float amp    = wave_amp * (1.0f - 0.22f * (float)layer);
            float base   = wave_base + (float)layer * (float)h * 0.035f;
            float thick  = (float)h * 0.045f;

            float cr = 0.12f + 0.06f * (float)layer;
            float cg = 0.38f + 0.12f * (float)layer;
            float cb = 0.95f - 0.08f * (float)layer;
            float ca = 0.40f - 0.08f * (float)layer;

            for (int i = 0; i < steps; i++) {
                float x0 = (float)i * dx;
                float x1 = x0 + dx;
                float y0_top = base + sinf(x0 * wave_freq + phase) * amp;
                float y1_top = base + sinf(x1 * wave_freq + phase) * amp;
                float y0_bot = y0_top + thick;
                float y1_bot = y1_top + thick;

                render_draw_triangle(x0, y0_top, x1, y1_top,
                                     x0, y0_bot, cr, cg, cb, ca);
                render_draw_triangle(x1, y1_top, x1, y1_bot,
                                     x0, y0_bot, cr, cg, cb, ca);
            }
        }
    }

    /* ── Title: "PlayOS" ── */
    float title_scale = (float)h / 240.0f;  /* Scale with screen height */
    const char *title = "PlayOS";
    float title_w = render_text_width(title, title_scale);
    float title_x = ((float)w - title_w) * 0.5f;
    float title_y = (float)h * 0.20f;

    render_draw_text(title, title_x, title_y, title_scale,
                     1.0f, 1.0f, 1.0f, 1.0f);

    /* ── Subtitle ── */
    float sub_scale = title_scale * 0.5f;
    const char *subtitle = "Gaming Console OS";
    float sub_w = render_text_width(subtitle, sub_scale);
    float sub_x = ((float)w - sub_w) * 0.5f;
    float sub_y = title_y + title_scale * 10.0f;

    render_draw_text(subtitle, sub_x, sub_y, sub_scale,
                     0.7f, 0.7f, 0.8f, 1.0f);

    /* ── Menu items ── */
    float menu_y = (float)h * 0.50f;
    float menu_scale = title_scale * 0.55f;

    static const char *menu_labels[2] = { "Library", "Settings" };
    for (int i = 0; i < 2; i++) {
        bool sel = (i == s->home_cursor);
        float item_y = menu_y + menu_scale * 12.0f * (float)i;

        if (sel) {
            /* Selection highlight background */
            float label_w = render_text_width(menu_labels[i], menu_scale);
            float box_w = label_w + menu_scale * 8.0f;
            render_draw_rect(((float)w - box_w) * 0.5f,
                             item_y - menu_scale * 1.0f,
                             box_w, menu_scale * 8.0f,
                             0.84f, 0.42f, 0.0f, 0.9f);
        }

        float label_w = render_text_width(menu_labels[i], menu_scale);
        render_draw_text(menu_labels[i], ((float)w - label_w) * 0.5f,
                         item_y, menu_scale,
                         sel ? 1.0f : 0.6f,
                         sel ? 1.0f : 0.6f,
                         sel ? 1.0f : 0.7f,
                         1.0f);
    }

    /* ── Menu navigation hint ── */
    const char *menu_hint = "[D-Pad] Navigate    [A] Select";
    float menu_hint_w = render_text_width(menu_hint, menu_scale * 0.7f);
    render_draw_text(menu_hint, ((float)w - menu_hint_w) * 0.5f,
                     menu_y + menu_scale * 12.0f * 2.0f + menu_scale * 2.0f,
                     menu_scale * 0.7f, 0.5f, 0.5f, 0.6f, 1.0f);

    /* ── System info footer ── */
    float footer_scale = sub_scale * 0.8f;
    float footer_y = (float)h - footer_scale * 50.0f;

    char footer[256];
    const char *device = playos_system_device_model();
    const char *os_ver = playos_system_os_version();
    snprintf(footer, sizeof(footer), "%s  |  PlayOS %s  |  Sprint 6",
             device ? device : "Unknown Device",
             os_ver ? os_ver : "0.3.0");

    float footer_w = render_text_width(footer, footer_scale);
    render_draw_text(footer, ((float)w - footer_w) * 0.5f, footer_y,
                     footer_scale, 0.4f, 0.4f, 0.5f, 1.0f);

    /* FPS counter (top-right, small) */
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "%.0f ms",
             s->frame_time * 1000.0);
    float fps_scale = footer_scale * 0.8f;
    float fps_w = render_text_width(fps_text, fps_scale);
    render_draw_text(fps_text, (float)w - fps_w - 16.0f, 8.0f,
                     fps_scale, 0.3f, 0.5f, 0.3f, 0.7f);
}
