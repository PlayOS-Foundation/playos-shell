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

static const char *const recovery_items[RECOVERY_ITEMS] = {
    "Reboot",
    "Shutdown",
    "Factory Reset",
    "Rollback",
    "View Logs",
};

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

    size_t from_len = strlen(from) > 0 && from[0] == '"' ? 18 : 0;
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

static void
recovery_draw_logs(struct playos_shell *s)
{
    float w = (float)s->output_width;
    float x = w * 0.12f;
    float y = 140.0f;
    float header_scale = 5.0f;
    float entry_scale = 4.0f;

    render_draw_text("System logs (/data/log)", x, 70.0f, header_scale,
                     1.0f, 1.0f, 1.0f, 1.0f);
    render_draw_text("B: Back", x, 105.0f, 2.5f,
                     0.6f, 0.6f, 0.6f, 1.0f);

    DIR *d = opendir("/data/log");
    if (!d) {
        render_draw_text("No logs available", x, y, entry_scale,
                         0.8f, 0.4f, 0.4f, 1.0f);
        return;
    }
    struct dirent *e;
    int shown = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        render_draw_text(e->d_name, x, y, entry_scale,
                         0.85f, 0.85f, 0.85f, 1.0f);
        y += entry_scale * 9.0f;
        shown++;
        if (shown >= 16)
            break;
    }
    closedir(d);
    if (shown == 0)
        render_draw_text("No logs available", x, y, entry_scale,
                         0.8f, 0.4f, 0.4f, 1.0f);
}

void
screen_recovery_enter(struct playos_shell *s)
{
    s->recovery_mode = 1;
    s->recovery_cursor = 0;
    s->recovery_confirm = 0;
    s->recovery_log_view = 0;
}

void
screen_recovery_update(struct playos_shell *s)
{
    if (s->recovery_log_view) {
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
            s->recovery_log_view = 1;
            break;
        }
    }
}

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
