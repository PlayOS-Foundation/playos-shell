/*
 * screen_recovery.c — PlayOS recovery menu (S14-T6)
 *
 * Shown when the shell is launched with PLAYOS_RECOVERY=1 (set by init when
 * recovery is entered: cmdline playos.recovery, data partition missing, or
 * repeated compositor failure). Provides: reboot, shutdown, factory reset,
 * rollback to the other A/B slot, and log viewing.
 */
#define _DEFAULT_SOURCE 1
#include "shell.h"
#include "playos-runtime/trusted_control.h"
#include "playos/playos_input.h"
#include "playos/playos_logging.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#define RECOVERY_ITEMS 5
#define RECOVERY_MAX_LOGS 24
#define RECOVERY_CONTENT_LINES 16
#define RECOVERY_CONTENT_LINE_LEN 200

static const char *const recovery_items[RECOVERY_ITEMS] = {
    "Reboot",
    "Shutdown",
    "Factory Reset",
    "Rollback",
    "View Logs",
};

/* ── Log helpers ─────────────────────────────────────────────────────── */

static void
recovery_log_load(struct playos_shell *s)
{
    s->recovery_log_count = 0;
    s->recovery_log_cursor = 0;
    s->recovery_log_content = 0;

    DIR *d = opendir("/data/log");
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && s->recovery_log_count < RECOVERY_MAX_LOGS) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(s->recovery_log_files[s->recovery_log_count],
                 sizeof(s->recovery_log_files[0]), "%s", e->d_name);
        s->recovery_log_count++;
    }
    closedir(d);
}

static void
recovery_draw_log_list(struct playos_shell *s)
{
    float w = (float)s->output_width;
    float x = w * 0.12f;
    float y = 170.0f;
    float header_scale = 5.0f;
    float entry_scale = 4.0f;
    float step = entry_scale * 10.0f;

    render_draw_text("System logs (/data/log)", x, 60.0f, header_scale,
                     1.0f, 1.0f, 1.0f, 1.0f);
    render_draw_text("A: View   B: Back", x, 115.0f, 2.5f,
                     0.6f, 0.6f, 0.6f, 1.0f);

    if (s->recovery_log_count == 0) {
        render_draw_text("No logs available", x, y, entry_scale,
                         0.8f, 0.4f, 0.4f, 1.0f);
        return;
    }

    for (int i = 0; i < s->recovery_log_count && i < 16; i++) {
        int sel = (i == s->recovery_log_cursor);
        render_draw_text(s->recovery_log_files[i], x, y, entry_scale,
                         sel ? 1.0f : 0.6f,
                         sel ? 0.8f : 0.6f,
                         sel ? 0.3f : 0.6f,
                         1.0f);
        y += step;
    }
}

static void
recovery_draw_log_content(struct playos_shell *s)
{
    float x = 60.0f;
    float y = 130.0f;
    float scale = 3.0f;
    float step = scale * 9.0f;

    render_draw_text(s->recovery_log_path, x, 40.0f, 4.0f,
                     1.0f, 1.0f, 1.0f, 1.0f);
    render_draw_text("B: Back to list", x, 85.0f, 2.5f,
                     0.6f, 0.6f, 0.6f, 1.0f);

    FILE *f = fopen(s->recovery_log_path, "r");
    if (!f) {
        render_draw_text("Cannot open log", x, y, scale,
                         0.8f, 0.4f, 0.4f, 1.0f);
        return;
    }

    /* Read the tail (last 8KB) so big logs stay responsive. */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long start = size > 8192 ? size - 8192 : 0;
    fseek(f, start, SEEK_SET);
    char buf[9000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    /* Keep the last RECOVERY_CONTENT_LINES lines. */
    char lines[RECOVERY_CONTENT_LINES][RECOVERY_CONTENT_LINE_LEN];
    int idx = 0, line_count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, "\n", &save); tok;
         tok = strtok_r(NULL, "\n", &save)) {
        size_t len = strlen(tok);
        if (len >= RECOVERY_CONTENT_LINE_LEN)
            len = RECOVERY_CONTENT_LINE_LEN - 1;
        memcpy(lines[idx], tok, len);
        lines[idx][len] = '\0';
        idx = (idx + 1) % RECOVERY_CONTENT_LINES;
        if (line_count < RECOVERY_CONTENT_LINES)
            line_count++;
    }

    int start_idx = line_count < RECOVERY_CONTENT_LINES ? 0 : idx;
    for (int i = 0; i < line_count; i++) {
        int pos = (start_idx + i) % RECOVERY_CONTENT_LINES;
        render_draw_text(lines[pos], x, y, scale, 0.85f, 0.85f, 0.85f, 1.0f);
        y += step;
    }
}

static void
recovery_draw_logs(struct playos_shell *s)
{
    if (s->recovery_log_content)
        recovery_draw_log_content(s);
    else
        recovery_draw_log_list(s);
}

/* ── Rollback ────────────────────────────────────────────────────────── */

static void
recovery_rollback(struct playos_shell *s)
{
    const char *path = "/EFI/playos/boot.json";
    FILE *f = fopen(path, "r");
    if (!f) {
        shell_set_toast(s, "boot.json not found");
        return;
    }
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    char *from = strstr(buf, "\"active_slot\":\"a\"");
    char *to = "\"active_slot\":\"b\"";
    if (!from) {
        from = strstr(buf, "\"active_slot\":\"b\"");
        to = "\"active_slot\":\"a\"";
    }
    if (!from) {
        shell_set_toast(s, "active_slot not found in boot.json");
        return;
    }

    /* Both "active_slot":"a" and "active_slot":"b" are 18 chars. */
    memcpy(from, to, 18);

    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *out = fopen(tmp, "w");
    if (!out) {
        shell_set_toast(s, "cannot write boot.json");
        return;
    }
    fwrite(buf, 1, n, out);
    fclose(out);
    chmod(tmp, 0600);
    rename(tmp, path);

    shell_set_toast(s, "Rolled back — rebooting");
    usleep(500000);
    playos_trusted_reboot(-1);
}

/* ── Screen entry / update ───────────────────────────────────────────── */

void
screen_recovery_enter(struct playos_shell *s)
{
    s->recovery_mode = 1;
    s->recovery_cursor = 0;
    s->recovery_confirm = 0;
    s->recovery_log_view = 0;
    s->recovery_log_content = 0;
    s->recovery_log_count = 0;
    s->recovery_log_cursor = 0;
}

void
screen_recovery_update(struct playos_shell *s)
{
    if (s->recovery_log_view) {
        if (s->recovery_log_content) {
            if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST))
                s->recovery_log_content = 0;
            return;
        }

        if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP) &&
            s->recovery_log_cursor > 0)
            s->recovery_log_cursor--;
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN) &&
            s->recovery_log_cursor < s->recovery_log_count - 1)
            s->recovery_log_cursor++;

        if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH) &&
            s->recovery_log_count > 0) {
            snprintf(s->recovery_log_path, sizeof(s->recovery_log_path),
                     "/data/log/%s",
                     s->recovery_log_files[s->recovery_log_cursor]);
            s->recovery_log_content = 1;
        }
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST))
            s->recovery_log_view = 0;
        return;
    }

    if (s->recovery_confirm) {
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
            PLAYOS_LOG_I("recovery", "factory reset requested");
            playos_trusted_factory_reset(-1, 1, 1, 1, 1, 1);
        }
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST))
            s->recovery_confirm = 0;
        return;
    }

    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP) &&
        s->recovery_cursor > 0)
        s->recovery_cursor--;
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN) &&
        s->recovery_cursor < RECOVERY_ITEMS - 1)
        s->recovery_cursor++;

    if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
        switch (s->recovery_cursor) {
        case 0: /* Reboot */
            playos_trusted_reboot(-1);
            break;
        case 1: /* Shutdown */
            playos_trusted_shutdown(-1);
            break;
        case 2: /* Factory Reset */
            s->recovery_confirm = 1;
            break;
        case 3: /* Rollback */
            recovery_rollback(s);
            break;
        case 4: /* View Logs */
            recovery_log_load(s);
            s->recovery_log_view = 1;
            break;
        }
    }
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

void
screen_recovery_draw(struct playos_shell *s)
{
    if (s->recovery_log_view) {
        recovery_draw_logs(s);
        return;
    }

    float w = (float)s->output_width;
    float h = (float)s->output_height;

    if (s->recovery_confirm) {
        float cs = 8.0f;
        float hs = 3.0f;
        float cw = render_text_width("Factory Reset?", cs);
        float hw = render_text_width("A: Confirm   B: Cancel", hs);
        render_draw_text("Factory Reset?", (w - cw) * 0.5f, h * 0.4f, cs,
                         1.0f, 1.0f, 1.0f, 1.0f);
        render_draw_text("A: Confirm   B: Cancel", (w - hw) * 0.5f,
                         h * 0.4f + cs * 8.0f, hs, 0.8f, 0.5f, 0.5f, 1.0f);
        return;
    }

    /* Title */
    float title_scale = 9.0f;
    float title_w = render_text_width("Recovery Mode", title_scale);
    render_draw_text("Recovery Mode", (w - title_w) * 0.5f, 60.0f,
                     title_scale, 1.0f, 1.0f, 1.0f, 1.0f);

    /* Menu items (shell text scale: actual px = scale * 7) */
    float item_scale = 5.0f;
    float item_step = item_scale * 11.0f;
    float menu_top = h * 0.32f;
    for (int i = 0; i < RECOVERY_ITEMS; i++) {
        int sel = (i == s->recovery_cursor);
        const char *label = recovery_items[i];
        float tw = render_text_width(label, item_scale);
        render_draw_text(label, (w - tw) * 0.5f, menu_top + i * item_step,
                         item_scale,
                         sel ? 1.0f : 0.55f,
                         sel ? 0.75f : 0.55f,
                         sel ? 0.2f : 0.55f,
                         1.0f);
    }

    /* Footer hint */
    float hint_scale = 2.5f;
    const char *hint = "D-pad: Navigate   A: Select   B: Back";
    float hint_w = render_text_width(hint, hint_scale);
    render_draw_text(hint, (w - hint_w) * 0.5f, h - 80.0f, hint_scale,
                     0.55f, 0.55f, 0.55f, 1.0f);
}
