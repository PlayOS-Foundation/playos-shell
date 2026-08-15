/**
 * screen_settings.c — Settings screen for PlayOS Shell
 *
 * Tabbed settings: Display, Audio, Power, System, Network, Input.
 * Displays system info from the Platform API.
 *
 * The tab strip is horizontally scrollable (the active tab is always kept
 * in view) and the content region is vertically scrollable so tabs can grow
 * extra info/input lines without overflowing the screen.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shell.h"
#include "playos/playos_system.h"
#include "playos/playos_logging.h"

#include <stdio.h>
#include <string.h>

#ifdef PLAYOS_TRUSTED_IPC
#include "playos-runtime/trusted_control.h"
#endif

/* ── Tab definitions ───────────────────────────────────────────────────── */

enum {
    TAB_DISPLAY = 0,
    TAB_AUDIO,
    TAB_POWER,
    TAB_SYSTEM,
    TAB_NETWORK,
    TAB_INPUT,
    TAB_COUNT
};

static const char *tab_names[TAB_COUNT] = {
    "Display",
    "Audio",
    "Power",
    "System",
    "Network",
    "Input",
};

static float
clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ── Content-region metrics (shared by update + draw) ─────────────────── */

static float
settings_header_scale(const struct playos_shell *s)
{
    return (float)s->output_height / 240.0f;
}

static float
settings_label_scale(const struct playos_shell *s)
{
    return settings_header_scale(s) * 0.5f;
}

/* Vertical advance of a single info line (label/value pair). */
static float
settings_line_step(const struct playos_shell *s)
{
    return settings_label_scale(s) * 16.0f;
}

/* Vertical advance of a selectable action row (e.g. Power Off / Restart). */
static float
settings_row_step(const struct playos_shell *s)
{
    return settings_label_scale(s) * 8.0f;
}

static float
settings_content_top(const struct playos_shell *s)
{
    return settings_header_scale(s) * 30.0f;
}

static float
settings_content_bottom(const struct playos_shell *s)
{
    /* Leave room for the navigation hint near the bottom edge. */
    return (float)s->output_height - settings_header_scale(s) * 22.0f;
}

static float
settings_viewport_height(const struct playos_shell *s)
{
    float h = settings_content_bottom(s) - settings_content_top(s);
    return h > 0.0f ? h : 0.0f;
}

/* Total pixel height of a tab's content, used to clamp vertical scroll. */
static float
settings_content_height(const struct playos_shell *s, int tab)
{
    float info_h = settings_line_step(s);
    float row_h  = settings_row_step(s);

    switch (tab) {
    case TAB_DISPLAY:
    case TAB_AUDIO:
    case TAB_POWER:
        return 3.0f * info_h;
    case TAB_SYSTEM:
        /* 4 info lines + gap + 2 selectable power rows. */
        return 4.0f * info_h + row_h + 2.0f * row_h;
    case TAB_NETWORK:
    case TAB_INPUT:
        return 4.0f * info_h;
    default:
        return 0.0f;
    }
}

/* Clamp the content scroll offset to [0, max] for the active tab. */
static void
settings_clamp_content_scroll(struct playos_shell *s)
{
    float max_scroll = settings_content_height(s, s->settings_tab)
                     - settings_viewport_height(s);
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    s->settings_content_scroll =
        clampf(s->settings_content_scroll, 0.0f, max_scroll);
}

/* ── Enter ─────────────────────────────────────────────────────────────── */

void
screen_settings_enter(struct playos_shell *s)
{
    s->settings_tab = TAB_DISPLAY;
    s->settings_tab_scroll = 0.0f;
    s->settings_content_scroll = 0.0f;
    s->settings_power_cursor = 0;
    s->power_confirm = false;
}

/* ── Update ────────────────────────────────────────────────────────────── */

void
screen_settings_update(struct playos_shell *s)
{
    /* ── Power confirmation modal ─────────────────────────────────────── */
    if (s->power_confirm) {
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
            /* A: confirm the selected power action. */
#ifdef PLAYOS_TRUSTED_IPC
            if (s->settings_power_cursor == 1) {
                PLAYOS_LOG_I("shell", "settings: reboot confirmed");
                playos_trusted_reboot(-1);
            } else {
                PLAYOS_LOG_I("shell", "settings: power-off confirmed");
                /* Orderly power-off never returns; this blocks until the
                 * kernel powers the machine down. */
                playos_trusted_shutdown(-1);
            }
#else
            PLAYOS_LOG_W("shell", "settings: power action requested "
                         "(trusted IPC not available)");
            s->power_confirm = false;
#endif
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
            /* B: cancel. */
            s->power_confirm = false;
        }
        return;
    }

    int prev_tab = s->settings_tab;

    /* D-pad L/R: switch tab (wraps around the strip). */
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

    /* Reset vertical scroll whenever the tab changes. */
    if (s->settings_tab != prev_tab)
        s->settings_content_scroll = 0.0f;

    /* D-pad U/D: vertical navigation / scrolling. */
    if (s->settings_tab == TAB_SYSTEM) {
        /* System tab: move between the two power actions. */
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP)) {
            if (s->settings_power_cursor > 0)
                s->settings_power_cursor--;
        }
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN)) {
            if (s->settings_power_cursor < 1)
                s->settings_power_cursor++;
        }
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
            s->power_confirm = true;
            return;
        }
    } else {
        /* Read-only tabs: scroll the info block so it can grow later. */
        float step = settings_line_step(s);
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP))
            s->settings_content_scroll -= step;
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN))
            s->settings_content_scroll += step;
        settings_clamp_content_scroll(s);
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

    /* Total width of all tabs. */
    float total_w = 0.0f;
    float tab_widths[TAB_COUNT];
    for (int i = 0; i < TAB_COUNT; i++) {
        tab_widths[i] = render_text_width(tab_names[i], scale) + tab_padding * 2.0f;
        total_w += tab_widths[i];
    }

    /* Left/right margin so the strip can breathe when it overflows. */
    float margin = scale * 6.0f;
    float view_w = (float)w - margin * 2.0f;

    float draw_x;
    bool clipped = false;

    if (total_w <= view_w) {
        /* Everything fits: center the strip, no scroll. */
        s->settings_tab_scroll = 0.0f;
        draw_x = ((float)w - total_w) * 0.5f;
    } else {
        /* Overflow: keep the active tab centered, clamped to the ends. */
        float active_x = 0.0f;
        for (int i = 0; i < s->settings_tab; i++)
            active_x += tab_widths[i];

        float target = active_x
                     - (view_w - tab_widths[s->settings_tab]) * 0.5f;
        target = clampf(target, 0.0f, total_w - view_w);

        /* Ease toward the target for a smooth cross-bar feel. */
        s->settings_tab_scroll += (target - s->settings_tab_scroll) * 0.25f;

        draw_x = margin - s->settings_tab_scroll;
        clipped = true;
    }

    if (clipped)
        render_begin_scissor((int)margin, (int)(y - scale * 1.0f),
                             (int)view_w, (int)(scale * 10.0f));

    for (int i = 0; i < TAB_COUNT; i++) {
        int is_active = (i == s->settings_tab);

        /* Skip tabs fully outside the clipped viewport. */
        if (draw_x + tab_widths[i] < margin - 1.0f ||
            draw_x > (float)w - margin + 1.0f) {
            draw_x += tab_widths[i];
            continue;
        }

        /* Tab background */
        if (is_active) {
            render_draw_rect(draw_x, y - scale * 0.5f,
                             tab_widths[i], scale * 7.0f,
                             0.84f, 0.42f, 0.0f, 0.9f);
        } else {
            render_draw_rect(draw_x, y - scale * 0.5f,
                             tab_widths[i], scale * 7.0f,
                             0.12f, 0.20f, 0.35f, 0.6f);
        }

        /* Tab label */
        float label_x = draw_x + tab_padding;
        float label_y = y + scale * 1.0f;
        render_draw_text(tab_names[i], label_x, label_y, scale,
                         is_active ? 1.0f : 0.6f,
                         is_active ? 1.0f : 0.6f,
                         is_active ? 1.0f : 0.6f,
                         1.0f);

        draw_x += tab_widths[i];
    }

    if (clipped)
        render_end_scissor();
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
    float header_scale = settings_header_scale(s);
    const char *header = "Settings";
    float header_w = render_text_width(header, header_scale);
    render_draw_text(header, ((float)w - header_w) * 0.5f,
                     20.0f, header_scale, 1.0f, 1.0f, 1.0f, 1.0f);

    /* ── Tab bar ── */
    float tab_scale = header_scale * 0.6f;
    draw_tab_bar(s, header_scale * 18.0f, tab_scale);

    /* ── Tab content ── */
    float content_x = (float)w * 0.15f;
    float label_scale = settings_label_scale(s);
    float value_scale = label_scale;

    /* Vertical scroll: content is clipped to a viewport so tabs can grow
     * extra info/input lines without overflowing the screen. */
    settings_clamp_content_scroll(s);
    float content_top = settings_content_top(s);
    float content_bottom = settings_content_bottom(s);
    render_begin_scissor(0, (int)content_top, w,
                         (int)(content_bottom - content_top));

    float content_y = content_top - s->settings_content_scroll;

    switch (s->settings_tab) {
    case TAB_DISPLAY: /* Display */
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

    case TAB_AUDIO: /* Audio */
        draw_info_line(s, "Output", "Built-in Speakers",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "Volume", "75%",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "Audio Driver", "ALSA",
                       content_x, &content_y, label_scale, value_scale);
        break;

    case TAB_POWER: /* Power */
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

            const char *profile = "Balanced";
            if (s->power_info_valid) {
                if (s->power_info.active_profile == PLAYOS_PERF_POWER_SAVE)
                    profile = "Power Save";
                else if (s->power_info.active_profile == PLAYOS_PERF_PERFORMANCE)
                    profile = "Performance";
            }
            draw_info_line(s, "Performance Mode", profile,
                           content_x, &content_y, label_scale, value_scale);
        }
        break;

    case TAB_SYSTEM: /* System */
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

            /* ── Power actions ── */
            content_y += label_scale * 8.0f;
            static const char *power_labels[2] = { "Power Off", "Restart" };
            float row_w = (float)w - content_x * 2.0f;
            for (int i = 0; i < 2; i++) {
                bool sel = (i == s->settings_power_cursor);
                if (sel) {
                    render_draw_rect(content_x,
                                     content_y - label_scale * 0.5f,
                                     row_w, label_scale * 7.0f,
                                     0.84f, 0.42f, 0.0f, 0.9f);
                }
                render_draw_text(power_labels[i], content_x, content_y,
                                 label_scale,
                                 sel ? 1.0f : 0.6f,
                                 sel ? 1.0f : 0.6f,
                                 sel ? 1.0f : 0.6f,
                                 1.0f);
                content_y += label_scale * 8.0f;
            }
        }
        break;

    case TAB_NETWORK: /* Network */
        draw_info_line(s, "Status", "Not Connected",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "Wi-Fi", "Disabled",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "Bluetooth", "Disabled",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "IP Address", "-",
                       content_x, &content_y, label_scale, value_scale);
        break;

    case TAB_INPUT: /* Input */
        draw_info_line(s, "Controller", "Built-in",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "D-Pad", "Active",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "Gyro", "Calibrated",
                       content_x, &content_y, label_scale, value_scale);
        draw_info_line(s, "Button Mapping", "Default",
                       content_x, &content_y, label_scale, value_scale);
        break;
    }

    render_end_scissor();

    /* ── Navigation hint ── */
    float hint_scale = header_scale * 0.45f;
    const char *hints = (s->settings_tab == TAB_SYSTEM)
                            ? "[D-Pad] Navigate    [A] Select    [B] Back"
                            : "[D-Pad] Switch Tab / Scroll    [B] Back";
    float hints_w = render_text_width(hints, hint_scale);
    render_draw_text(hints, ((float)w - hints_w) * 0.5f,
                     (float)h - hint_scale * 45.0f,
                     hint_scale, 0.5f, 0.5f, 0.6f, 1.0f);

    /* ── Power confirmation modal ── */
    if (s->power_confirm) {
        render_draw_rect(0.0f, 0.0f, (float)w, (float)h,
                         0.0f, 0.0f, 0.0f, 0.7f);

        float modal_scale = header_scale * 0.55f;
        const char *action = (s->settings_power_cursor == 1)
                                 ? "Restart PlayOS?"
                                 : "Power off PlayOS?";
        float action_w = render_text_width(action, modal_scale);
        render_draw_text(action, ((float)w - action_w) * 0.5f,
                         (float)h * 0.40f, modal_scale,
                         1.0f, 1.0f, 1.0f, 1.0f);

        const char *confirm_hint = "A: Confirm    B: Cancel";
        float confirm_w = render_text_width(confirm_hint, modal_scale * 0.6f);
        render_draw_text(confirm_hint, ((float)w - confirm_w) * 0.5f,
                         (float)h * 0.52f, modal_scale * 0.6f,
                         0.8f, 0.8f, 0.9f, 1.0f);
    }
}
