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

    /* ── Animated accent bars (test-client pattern) ── */
    float bar_speed = 0.3f;
    float bar_width = (float)w * 0.08f;

    for (int i = 0; i < 3; i++) {
        float phase = (float)(s->elapsed_time * bar_speed + (double)i * 0.33);
        float offset = (float)(fmod(phase, 2.0) - 1.0) * (float)w;

        float brightness = 0.6f + 0.4f * sinf((float)(s->elapsed_time * 3.0 + (double)i));
        float cr = 0.84f * brightness;
        float cg = 0.42f * brightness;
        float cb = 0.0f;
        float ca = 0.3f + 0.5f * brightness;

        render_draw_rect(offset - bar_width * 0.5f, 0.0f,
                         bar_width, (float)h, cr, cg, cb, ca);
    }

    /* ── Title: "PlayOS" ── */
    float title_scale = (float)h / 100.0f;  /* Scale with screen height */
    const char *title = "PlayOS";
    float title_w = render_text_width(title, title_scale);
    float title_x = ((float)w - title_w) * 0.5f;
    float title_y = (float)h * 0.20f;

    render_draw_text(title, title_x, title_y, title_scale,
                     1.0f, 1.0f, 1.0f, 1.0f);

    /* ── Subtitle ── */
    float sub_scale = title_scale * 0.4f;
    const char *subtitle = "Gaming Console OS";
    float sub_w = render_text_width(subtitle, sub_scale);
    float sub_x = ((float)w - sub_w) * 0.5f;
    float sub_y = title_y + title_scale * 10.0f;

    render_draw_text(subtitle, sub_x, sub_y, sub_scale,
                     0.7f, 0.7f, 0.8f, 1.0f);

    /* ── Menu items ── */
    float menu_y = (float)h * 0.50f;
    float menu_scale = title_scale * 0.35f;

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
    float menu_hint_w = render_text_width(menu_hint, menu_scale * 0.6f);
    render_draw_text(menu_hint, ((float)w - menu_hint_w) * 0.5f,
                     menu_y + menu_scale * 12.0f * 2.0f + menu_scale * 2.0f,
                     menu_scale * 0.6f, 0.5f, 0.5f, 0.6f, 1.0f);

    /* ── System info footer ── */
    float footer_scale = sub_scale * 0.6f;
    float footer_y = (float)h - footer_scale * 30.0f;

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
