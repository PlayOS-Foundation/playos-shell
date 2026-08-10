/**
 * screen_settings.c — Settings screen for PlayOS Shell
 *
 * Tabbed settings: Display, Audio, Power, System.
 * Displays system info from the Platform API.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shell.h"
#include "playos/playos_system.h"

#include <stdio.h>
#include <string.h>

/* ── Tab definitions ───────────────────────────────────────────────────── */

static const char *tab_names[] = {
    "Display",
    "Audio",
    "Power",
    "System",
};
#define TAB_COUNT (int)(sizeof(tab_names) / sizeof(tab_names[0]))

/* ── Enter ─────────────────────────────────────────────────────────────── */

void
screen_settings_enter(struct playos_shell *s)
{
    s->settings_tab = 0;
}

/* ── Update ────────────────────────────────────────────────────────────── */

void
screen_settings_update(struct playos_shell *s)
{
    /* D-pad L/R: switch tab */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_LEFT)) {
        if (s->settings_tab > 0)
            s->settings_tab--;
        else
            s->settings_tab = TAB_COUNT - 1;
    }
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_RIGHT)) {
        if (s->settings_tab < TAB_COUNT - 1)
            s->settings_tab++;
        else
            s->settings_tab = 0;
    }

    /* B: back to home */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
        s->current_screen = SCREEN_HOME;
        screen_home_enter(s);
        return;
    }
}

/* ── Draw helpers ──────────────────────────────────────────────────────── */

static void
draw_tab_bar(struct playos_shell *s, float y, float scale)
{
    int w = s->output_width;
    float tab_padding = scale * 4.0f;

    /* Calculate total tab bar width */
    float total_w = 0.0f;
    float tab_widths[TAB_COUNT];
    for (int i = 0; i < TAB_COUNT; i++) {
        tab_widths[i] = render_text_width(tab_names[i], scale) + tab_padding * 2.0f;
        total_w += tab_widths[i];
    }

    float cursor_x = ((float)w - total_w) * 0.5f;

    for (int i = 0; i < TAB_COUNT; i++) {
        int is_active = (i == s->settings_tab);

        /* Tab background */
        if (is_active) {
            render_draw_rect(cursor_x, y - scale * 0.5f,
                             tab_widths[i], scale * 7.0f,
                             0.84f, 0.42f, 0.0f, 0.9f);
        } else {
            render_draw_rect(cursor_x, y - scale * 0.5f,
                             tab_widths[i], scale * 7.0f,
                             0.12f, 0.20f, 0.35f, 0.6f);
        }

        /* Tab label */
        float label_x = cursor_x + tab_padding;
        float label_y = y + scale * 1.0f;
        render_draw_text(tab_names[i], label_x, label_y, scale,
                         is_active ? 1.0f : 0.6f,
                         is_active ? 1.0f : 0.6f,
                         is_active ? 1.0f : 0.6f,
                         1.0f);

        cursor_x += tab_widths[i];
    }
}

static void
draw_info_line(struct playos_shell *s, const char *label, const char *value,
               float x, float *y, float label_scale, float value_scale)
{
    int w = s->output_width;

    /* Label (left-aligned) */
    render_draw_text(label, x, *y, label_scale, 0.6f, 0.6f, 0.7f, 1.0f);
    (void) render_text_width(label, label_scale);

    /* Value (right side) */
    if (value && value[0]) {
        float value_w = render_text_width(value, value_scale);
        render_draw_text(value, (float)w - x - value_w, *y,
                         value_scale, 0.9f, 0.9f, 0.9f, 1.0f);
    }

    *y += label_scale * 16.0f;
}

/* ── Draw ──────────────────────────────────────────────────────────────── */

void
screen_settings_draw(struct playos_shell *s)
{
    int w = s->output_width;
    int h = s->output_height;

    /* ── Background ── */
    render_begin_frame(0.06f, 0.12f, 0.22f, 1.0f);

    /* ── Header ── */
    float header_scale = (float)h / 50.0f;
    const char *header = "Settings";
    float header_w = render_text_width(header, header_scale);
    render_draw_text(header, ((float)w - header_w) * 0.5f,
                     20.0f, header_scale, 1.0f, 1.0f, 1.0f, 1.0f);

    /* ── Tab bar ── */
    float tab_scale = header_scale * 0.4f;
    draw_tab_bar(s, header_scale * 18.0f, tab_scale);

    /* ── Tab content ── */
    float content_x = (float)w * 0.15f;
    float content_y = header_scale * 30.0f;
    float label_scale = header_scale * 0.35f;
    float value_scale = label_scale;

    switch (s->settings_tab) {
    case 0: /* Display */
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%dx%d", s->output_width,
                     s->output_height);
            draw_info_line(s, "Resolution", buf,
                           content_x, &content_y, label_scale, value_scale);

            snprintf(buf, sizeof(buf), "%.1f", s->dpi_scale);
            draw_info_line(s, "DPI Scale", buf,
                           content_x, &content_y, label_scale, value_scale);

            const char *gpu = playos_system_gpu_description();
            draw_info_line(s, "GPU", gpu,
                           content_x, &content_y, label_scale, value_scale);
        }
        break;

    case 1: /* Audio */
        draw_info_line(s, "Output", "Built-in Speakers",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "Volume", "75%",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "Audio Driver", "ALSA (PipeWire)",
                       content_x, &content_y, label_scale, value_scale);
        break;

    case 2: /* Power */
        {
            char buf[64];
            uint64_t total_mem = playos_system_total_memory_bytes();
            snprintf(buf, sizeof(buf), "%llu MB",
                     (unsigned long long)(total_mem / (1024 * 1024)));
            draw_info_line(s, "Total Memory", buf,
                           content_x, &content_y, label_scale, value_scale);

            uint64_t avail_mem = playos_system_available_memory_bytes();
            snprintf(buf, sizeof(buf), "%llu MB",
                     (unsigned long long)(avail_mem / (1024 * 1024)));
            draw_info_line(s, "Available Memory", buf,
                           content_x, &content_y, label_scale, value_scale);

            draw_info_line(s, "Performance Mode", "Balanced",
                           content_x, &content_y, label_scale, value_scale);
        }
        break;

    case 3: /* System */
        {
            const char *device = playos_system_device_model();
            draw_info_line(s, "Device", device,
                           content_x, &content_y, label_scale, value_scale);

            const char *os_ver = playos_system_os_version();
            draw_info_line(s, "OS Version", os_ver,
                           content_x, &content_y, label_scale, value_scale);

            const char *cpu = playos_system_cpu_description();
            draw_info_line(s, "CPU", cpu,
                           content_x, &content_y, label_scale, value_scale);

            const char *locale = playos_system_locale();
            draw_info_line(s, "Locale", locale,
                           content_x, &content_y, label_scale, value_scale);
        }
        break;
    }

    /* ── Navigation hint ── */
    float hint_scale = header_scale * 0.25f;
    const char *hints = "[D-Pad] Switch Tab    [B] Back";
    float hints_w = render_text_width(hints, hint_scale);
    render_draw_text(hints, ((float)w - hints_w) * 0.5f,
                     (float)h - hint_scale * 20.0f,
                     hint_scale, 0.5f, 0.5f, 0.6f, 1.0f);
}
