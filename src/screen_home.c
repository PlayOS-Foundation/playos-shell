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
    (void)s;
    /* Nothing to initialize */
}

/* ── Update ────────────────────────────────────────────────────────────── */

void
screen_home_update(struct playos_shell *s)
{
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
        /* A button → Library */
        s->current_screen = SCREEN_LIBRARY;
        screen_library_enter(s);
        return;
    }

    if (shell_input_button_pressed(s, PLAYOS_BUTTON_NORTH)) {
        /* Y button → Settings */
        s->current_screen = SCREEN_SETTINGS;
        screen_settings_enter(s);
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
    float pulse = 0.5f + 0.5f * sinf((float)s->elapsed_time * 2.0f);

    /* "Press (A)  Library" */
    const char *lib_text = "Press [A]  Library";
    float lib_w = render_text_width(lib_text, menu_scale);
    render_draw_text(lib_text, ((float)w - lib_w) * 0.5f, menu_y, menu_scale,
                     0.84f, 0.42f, 0.0f, 0.7f + 0.3f * pulse);

    /* "Press (Y)  Settings" */
    const char *set_text = "Press [Y]  Settings";
    float set_w = render_text_width(set_text, menu_scale);
    render_draw_text(set_text, ((float)w - set_w) * 0.5f,
                     menu_y + menu_scale * 12.0f, menu_scale,
                     0.6f, 0.6f, 0.7f, 1.0f);

    /* ── System info footer ── */
    float footer_scale = sub_scale * 0.6f;
    float footer_y = (float)h - footer_scale * 30.0f;

    char footer[256];
    const char *device = playos_system_device_model();
    const char *os_ver = playos_system_os_version();
    snprintf(footer, sizeof(footer), "%s  |  PlayOS %s  |  Sprint 5",
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
