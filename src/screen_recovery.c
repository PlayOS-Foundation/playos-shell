/*
 * screen_recovery.c — PlayOS recovery menu (S14-T6)
 *
 * Shown when the shell is launched with PLAYOS_RECOVERY=1 (set by init when
 * recovery is entered: cmdline playos.recovery, data partition missing, or
 * repeated compositor failure). Provides: reboot, shutdown, factory reset,
 * rollback to the other A/B slot, and log viewing.
 */
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
    float x = (float)s->output_width * 0.15f;
    float y = 140.0f;
    float scale = 20.0f;

    render_draw_text("System logs (/data/log)", x, 80.0f, 32.0f,
                     1.0f, 1.0f, 1.0f, 1.0f);
    render_draw_text("B: Back", x, 110.0f, 18.0f,
                     0.6f, 0.6f, 0.6f, 1.0f);

    DIR *d = opendir("/data/log");
    if (!d) {
        render_draw_text("No logs available", x, y, scale,
                         0.8f, 0.4f, 0.4f, 1.0f);
        return;
    }
    struct dirent *e;
    int shown = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        render_draw_text(e->d_name, x, y, scale, 0.85f, 0.85f, 0.85f, 1.0f);
        y += scale * 1.6f;
        shown++;
        if (shown >= 16)
            break;
    }
    closedir(d);
    if (shown == 0)
        render_draw_text("No logs available", x, y, scale,
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
        render_draw_text("Factory Reset?", w * 0.5f - 180.0f, h * 0.4f, 40.0f,
                         1.0f, 1.0f, 1.0f, 1.0f);
        render_draw_text("A: Confirm   B: Cancel", w * 0.5f - 200.0f,
                         h * 0.4f + 60.0f, 24.0f, 0.8f, 0.5f, 0.5f, 1.0f);
        return;
    }

    render_draw_text("Recovery Mode", w * 0.5f - 120.0f, 80.0f, 44.0f,
                     1.0f, 1.0f, 1.0f, 1.0f);

    float x = w * 0.5f - 160.0f;
    float y = 200.0f;
    float scale = 28.0f;

    for (int i = 0; i < RECOVERY_ITEMS; i++) {
        int sel = (i == s->recovery_cursor);
        render_draw_text(recovery_items[i], x, y, scale,
                         sel ? 1.0f : 0.6f,
                         sel ? 0.9f : 0.6f,
                         sel ? 0.4f : 0.6f,
                         1.0f);
        y += scale * 1.8f;
    }

    render_draw_text("D-pad: Navigate   A: Select   B: Back", x, y + 20.0f,
                     18.0f, 0.55f, 0.55f, 0.55f, 1.0f);
}
