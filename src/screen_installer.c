/*
 * screen_installer.c — PlayOS installer front-end (S14-T10)
 *
 * The runtime installer used to be a separate, "foreign"-looking UI that
 * appeared only after init had already torn the session down. This screen makes
 * installing an app: it is reached from Settings, it is drawn with the shell's
 * own fonts/layout, and it does the two things the user must decide —
 * which disk, and the destructive confirmation — inside the shell.
 *
 * On confirm the shell sends the chosen disk through StartInstaller and init
 * performs the proven handoff (stop shell/overlay/compositor, keep the dev SSH
 * key, unmount /data + /EFI, restart the compositor, continue installing). The
 * installer child receives PLAYOS_INSTALL_TARGET and goes straight to the
 * destructive phase, so the user is never asked to pick a disk twice.
 */
#define _DEFAULT_SOURCE 1
#include "shell.h"
#include "playos-runtime/trusted_control.h"
#include "playos/playos_input.h"
#include "playos/playos_logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

/* A destructive action needs a deliberate gesture, not a tap. */
#define INSTALLER_HOLD_SECONDS 3.0

/* ── Disk discovery ─────────────────────────────────────────────────────
 * The shell only needs enough to present the choice; the installer re-checks
 * the device and refuses anything it does not recognise. We read /sys/block
 * directly (as the installer's own enumerator does) and skip virtual devices
 * plus the medium we booted from, so the user cannot erase the running system
 * by accident. */

static int
installer_is_virtual(const char *name)
{
    static const char *const prefixes[] = {
        "loop", "ram", "zram", "dm-", "md", "sr", "fd", "nbd", "rbd",
    };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(name, prefixes[i], n) == 0)
            return 1;
    }
    return 0;
}

static void
installer_read_text(const char *path, char *out, size_t outsz)
{
    out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    if (fgets(out, (int)outsz, f)) {
        size_t n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' '))
            out[--n] = '\0';
    }
    fclose(f);
}

/* "nvme0n1p2" -> "nvme0n1", "sda3" -> "sda": strip the partition suffix and the
 * 'p' separator nvme/mmcblk use. */
static void
installer_base_name(const char *dev, char *out, size_t outsz)
{
    const char *base = strrchr(dev, '/');
    base = base ? base + 1 : dev;

    size_t len = strlen(base);
    size_t digits = 0;
    while (digits < len && isdigit((unsigned char)base[len - 1 - digits]))
        digits++;
    if (digits > 0) {
        size_t cut = len - digits;
        if (cut > 0 && base[cut - 1] == 'p')
            cut--;
        len = cut;
    }
    if (len == 0)
        len = strlen(base);
    if (len >= outsz)
        len = outsz - 1;

    snprintf(out, outsz, "%.*s", (int)len, base);
}

/* The medium we are running from: the disk carrying the live session's /data
 * and root filesystem.
 *
 * /EFI is deliberately NOT consulted. On a live USB the ESP of the previously
 * installed system is mounted there (init keeps it mounted to read/write
 * boot.json), so treating it as "the boot disk" hid the real install target:
 * on the Ally it excluded the internal NVMe and the picker reported "no
 * suitable internal disk" even though the USB was the boot medium. */
static void
installer_boot_disk(char *out, size_t outsz)
{
    static const char *const wanted[] = { "/data", "/" };

    out[0] = '\0';

    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]) && !out[0]; i++) {
        FILE *f = fopen("/proc/mounts", "r");
        if (!f)
            return;

        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char dev[128], mnt[256];
            if (sscanf(line, "%127s %255s", dev, mnt) != 2)
                continue;
            if (strcmp(mnt, wanted[i]) != 0)
                continue;
            /* A live session's root is "rootfs" (squashfs, no /dev node) —
             * only a real block device identifies the medium. */
            if (strncmp(dev, "/dev/", 5) != 0)
                continue;
            installer_base_name(dev, out, outsz);
            break;
        }
        fclose(f);
    }
}

static void
installer_scan_disks(struct playos_shell *s)
{
    s->installer_count = 0;
    s->installer_cursor = 0;

    char boot_disk[64];
    installer_boot_disk(boot_disk, sizeof(boot_disk));

    DIR *d = opendir("/sys/block");
    if (!d) {
        PLAYOS_LOG_W("shell", "installer: /sys/block unavailable");
        return;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL &&
           s->installer_count < SHELL_INSTALLER_MAX_DISKS) {
        const char *name = e->d_name;
        char dev_name[64];

        if (name[0] == '.' || installer_is_virtual(name))
            continue;
        /* d_name can be up to NAME_MAX; /sys/block entries never are, but the
         * compiler cannot know that, so bound it explicitly (memcpy, not
         * snprintf, so no truncation warning). */
        size_t name_len = strlen(name);
        if (name_len >= sizeof(dev_name)) {
            PLAYOS_LOG_W("shell", "installer: ignoring odd block name");
            continue;
        }
        memcpy(dev_name, name, name_len + 1);

        if (boot_disk[0] && strcmp(dev_name, boot_disk) == 0) {
            PLAYOS_LOG_I("shell", "installer: skipping boot disk %s", dev_name);
            continue;
        }

        char path[192];

        char removable[8];
        snprintf(path, sizeof(path), "/sys/block/%s/removable", dev_name);
        installer_read_text(path, removable, sizeof(removable));
        if (removable[0] == '1') {
            PLAYOS_LOG_I("shell", "installer: skipping removable %s", dev_name);
            continue;
        }

        char sectors[32];
        snprintf(path, sizeof(path), "/sys/block/%s/size", dev_name);
        installer_read_text(path, sectors, sizeof(sectors));
        unsigned long long bytes =
            strtoull(sectors, NULL, 10) * 512ULL;

        char model[64];
        snprintf(path, sizeof(path), "/sys/block/%s/device/model", dev_name);
        installer_read_text(path, model, sizeof(model));
        if (model[0] == '\0')
            snprintf(model, sizeof(model), "disk");

        int idx = s->installer_count++;
        snprintf(s->installer_path[idx], sizeof(s->installer_path[idx]),
                 "/dev/%s", dev_name);
        snprintf(s->installer_label[idx], sizeof(s->installer_label[idx]),
                 "%s — %llu GB", model, bytes / (1000ULL * 1000ULL * 1000ULL));

        PLAYOS_LOG_I("shell", "installer: candidate %s (%s)",
                     s->installer_path[idx], s->installer_label[idx]);
    }

    closedir(d);
}

/* ── Screen hooks ─────────────────────────────────────────────────────── */

void
screen_installer_enter(struct playos_shell *s)
{
    s->installer_confirm = false;
    s->installer_hold_start = 0.0;
    installer_scan_disks(s);
    PLAYOS_LOG_I("shell", "installer front-end: %d candidate disk(s)",
                 s->installer_count);
}

void
screen_installer_update(struct playos_shell *s)
{
    if (s->installer_count <= 0) {
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
            s->current_screen = SCREEN_SETTINGS;
            screen_settings_enter(s);
        }
        return;
    }

    if (s->installer_confirm) {
        /* Hold A for three seconds: a tap must never erase a disk. */
        if (shell_input_button_held(s, PLAYOS_BUTTON_SOUTH)) {
            if (s->installer_hold_start == 0.0)
                s->installer_hold_start = s->elapsed_time;
            if (s->elapsed_time - s->installer_hold_start >=
                INSTALLER_HOLD_SECONDS) {
                const char *target = s->installer_path[s->installer_cursor];
                PLAYOS_LOG_I("shell", "installer: starting install to %s",
                             target);
                s->installer_confirm = false;
                s->installer_hold_start = 0.0;
#ifdef PLAYOS_TRUSTED_IPC
                if (playos_trusted_start_installer_target(-1, target) == 0)
                    shell_set_toast(s, "Starting installer…");
                else
                    shell_set_toast(s, "Installer start failed");
#else
                shell_set_toast(s, "Installer unavailable (no trusted IPC)");
#endif
                s->current_screen = SCREEN_SETTINGS;
                screen_settings_enter(s);
                return;
            }
        } else {
            s->installer_hold_start = 0.0;
        }

        if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
            s->installer_confirm = false;
            s->installer_hold_start = 0.0;
        }
        return;
    }

    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP))
        s->installer_cursor = (s->installer_cursor - 1 + s->installer_count) %
                              s->installer_count;
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN))
        s->installer_cursor = (s->installer_cursor + 1) % s->installer_count;

    if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH))
        s->installer_confirm = true;

    if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
        s->current_screen = SCREEN_SETTINGS;
        screen_settings_enter(s);
    }
}

void
screen_installer_draw(struct playos_shell *s)
{
    /* Every screen owns a full-frame clear: the shell's renderer keeps the
     * previous frame's pixels, so without this the Settings screen stayed
     * visible underneath and the two overlapped (seen on hardware). */
    render_begin_frame(0.06f, 0.12f, 0.22f, 1.0f);

    float w = (float)s->output_width;
    float h = (float)s->output_height;

    float title_scale = 9.0f;
    float title_w = render_text_width("Install PlayOS", title_scale);
    render_draw_text("Install PlayOS", (w - title_w) * 0.5f, 60.0f,
                     title_scale, 1.0f, 1.0f, 1.0f, 1.0f);

    if (s->installer_count <= 0) {
        const char *msg = "No suitable internal disk found.";
        float ms = 4.0f;
        float mw = render_text_width(msg, ms);
        render_draw_text(msg, (w - mw) * 0.5f, h * 0.45f, ms,
                         1.0f, 0.6f, 0.6f, 1.0f);

        const char *hint = "B: Back";
        float hs = 2.5f;
        float hw = render_text_width(hint, hs);
        render_draw_text(hint, (w - hw) * 0.5f,
                         h - hs * 45.0f, hs,
                         0.55f, 0.55f, 0.55f, 1.0f);
        return;
    }

    if (s->installer_confirm) {
        const char *path = s->installer_path[s->installer_cursor];
        char line[256];
        float ls = 5.0f;
        float x;

        snprintf(line, sizeof(line), "Install to %s?", path);
        x = (w - render_text_width(line, ls)) * 0.5f;
        render_draw_text(line, x, h * 0.30f, ls, 1.0f, 1.0f, 1.0f, 1.0f);

        {
            const char *warn = "This erases ALL DATA on that disk.";
            float ws = 3.5f;
            x = (w - render_text_width(warn, ws)) * 0.5f;
            render_draw_text(warn, x, h * 0.30f + ls * 9.0f, ws,
                             1.0f, 0.45f, 0.45f, 1.0f);
        }

        /* Hold progress bar. */
        float bar_w = w * 0.45f;
        float bar_h = 22.0f;
        float bar_x = (w - bar_w) * 0.5f;
        float bar_y = h * 0.52f;
        float progress = 0.0f;
        if (s->installer_hold_start > 0.0) {
            progress = (float)((s->elapsed_time - s->installer_hold_start) /
                               INSTALLER_HOLD_SECONDS);
            if (progress > 1.0f)
                progress = 1.0f;
        }
        render_draw_rect(bar_x, bar_y, bar_w, bar_h, 0.16f, 0.16f, 0.20f, 1.0f);
        render_draw_rect(bar_x, bar_y, bar_w * progress, bar_h,
                         0.47f, 0.27f, 0.78f, 1.0f);

        {
            const char *prompt = "Hold A to erase and install";
            float ps = 3.0f;
            x = (w - render_text_width(prompt, ps)) * 0.5f;
            render_draw_text(prompt, x, bar_y + bar_h + 24.0f, ps,
                             0.9f, 0.9f, 0.9f, 1.0f);
        }
        {
            const char *hint = "B: Cancel";
            float hs = 2.5f;
            x = (w - render_text_width(hint, hs)) * 0.5f;
            render_draw_text(hint, x, h - hs * 45.0f, hs,
                             0.55f, 0.55f, 0.55f, 1.0f);
        }
        return;
    }

    /* Target list. */
    {
        const char *sub = "Choose the internal disk PlayOS will be installed to.";
        float ss = 3.2f;
        float x = (w - render_text_width(sub, ss)) * 0.5f;
        render_draw_text(sub, x, 150.0f, ss, 0.75f, 0.75f, 0.80f, 1.0f);
    }
    {
        const char *warn = "Existing data on the chosen disk will be destroyed.";
        float ws = 3.0f;
        float x = (w - render_text_width(warn, ws)) * 0.5f;
        render_draw_text(warn, x, 190.0f, ws, 1.0f, 0.55f, 0.55f, 1.0f);
    }

    float item_scale = 4.5f;
    float item_step = item_scale * 12.0f;
    float menu_top = h * 0.34f;
    for (int i = 0; i < s->installer_count; i++) {
        int sel = (i == s->installer_cursor);
        char row[200];
        snprintf(row, sizeof(row), "%s  %s", s->installer_path[i],
                 s->installer_label[i]);
        float tw = render_text_width(row, item_scale);
        render_draw_text(row, (w - tw) * 0.5f, menu_top + i * item_step,
                         item_scale,
                         sel ? 1.00f : 0.60f,
                         sel ? 0.75f : 0.60f,
                         sel ? 0.20f : 0.60f,
                         1.0f);
        if (sel) {
            float marker_w = render_text_width(">", item_scale);
            render_draw_text(">", (w - tw) * 0.5f - marker_w * 1.6f,
                             menu_top + i * item_step, item_scale,
                             1.0f, 0.75f, 0.20f, 1.0f);
        }
    }

    {
        const char *hint = "D-pad: Select   A: Continue   B: Back";
        float hs = 2.5f;
        float x = (w - render_text_width(hint, hs)) * 0.5f;
        render_draw_text(hint, x, h - hs * 45.0f, hs,
                             0.55f, 0.55f, 0.55f, 1.0f);
    }
}
