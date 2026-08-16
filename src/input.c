/**
 * input.c — Direct evdev controller input (trusted)
 *
 * The shell reads ALL controller input directly from /dev/input/event*.
 * This is the pragmatic Sprint 5 approach (S5-T5 option 1): the shell
 * is a trusted system component and needs reserved buttons (SYSTEM/QUICK_MENU)
 * that libplayos' input API strips from game processes. The shell opens
 * its own fd and decodes standard face buttons, d-pad (both ABS_HAT and
 * BTN_DPAD_* forms), shoulder buttons, stick clicks, and reserved buttons.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "shell.h"
#include "playos/playos_audio.h"
#include "playos/playos_input.h"
#include "playos/playos_logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>

/* ── Bit array helpers (raw evdev, not libevdev) ──────────────────────── */
#define BITS_PER_LONG  (sizeof(unsigned long) * 8)
#define NBITS(x)       (((unsigned long)(x) / BITS_PER_LONG) + 1)
#define EVDEV_BITS(x)  NBITS(x)
#define TEST_BIT(bit, array) \
    (((array)[(unsigned long)(bit) / BITS_PER_LONG] >> \
      ((unsigned long)(bit) % BITS_PER_LONG)) & 1)

/* ── Finding the gamepad ─────────────────────────────────────────────── */

/* Quick check: does this event device have gamepad axes + buttons?
 *
 * Detection matches the Platform API (backend_evdev.c open_controller):
 * requires all 4 stick axes (ABS_X, ABS_Y, ABS_RX, ABS_RY) and BTN_SOUTH.
 * No d-pad capability check — some drivers (hid-asus on ROG Ally) report
 * d-pad events at runtime without advertising the bits in the evdev
 * capability mask.  The event loop handles both ABS_HAT and BTN_DPAD_*
 * forms regardless of whether they were advertised. */
static int is_gamepad_device(int fd)
{
    unsigned long abs_bits[EVDEV_BITS(ABS_MAX)] = {0};
    unsigned long key_bits[EVDEV_BITS(KEY_MAX)] = {0};

    /* Get device name for diagnostics */
    char name[256] = {0};
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);

    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0)
        return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
        return 0;

    /* Must have all 4 stick axes (matches Platform API detection) */
    if (!TEST_BIT(ABS_X,  abs_bits) || !TEST_BIT(ABS_Y,  abs_bits) ||
        !TEST_BIT(ABS_RX, abs_bits) || !TEST_BIT(ABS_RY, abs_bits)) {
        PLAYOS_LOG_D("input", "skip '%s': missing stick axes", name);
        return 0;
    }

    /* Must have at least the face buttons */
    if (!TEST_BIT(BTN_SOUTH, key_bits)) {
        PLAYOS_LOG_D("input", "skip '%s': missing face buttons", name);
        return 0;
    }

    /* Note: we intentionally do NOT check for d-pad capability here.
     * Some drivers (hid-asus on ROG Ally) report d-pad events at runtime
     * via BTN_DPAD_* or ABS_HAT0X/Y without advertising those bits in
     * the evdev capability mask.  The event loop in shell_input_poll()
     * handles both forms regardless. */

    return 1;
}

static int find_gamepad_device(void)
{
    PLAYOS_LOG_I("input", "scanning /dev/input/event* for gamepad...");

    DIR *dir = opendir("/dev/input");
    if (!dir) {
        PLAYOS_LOG_W("input", "cannot open /dev/input: %s", strerror(errno));
        return -1;
    }

    int best_fd = -1;
    int scanned = 0, skipped = 0;
    char best_name[256] = {0};

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;
        scanned++;

        char path[320];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            skipped++;
            continue;
        }

        if (is_gamepad_device(fd)) {
            /* Check for Xbox/ASUS gamepad by name */
            char name[256] = {0};
            ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);

            /* Prefer Xbox/Ally controllers over generic HID */
            if (strstr(name, "Xbox") || strstr(name, "xbox") ||
                strstr(name, "X-Box") ||
                strstr(name, "Microsoft") ||
                strstr(name, "ASUE") ||        /* ASUS ROG Ally (all models) */
                strstr(name, "ASUS") ||
                strstr(name, "ROG Ally") ||
                strstr(name, "Gamepad")) {     /* Catch "ASUE* Gamepad" etc. */
                PLAYOS_LOG_I("input", "found gamepad: '%s' (%s) fd=%d",
                             name, path, fd);
                closedir(dir);
                return fd;
            }

            /* Keep the first viable fallback */
            if (best_fd < 0) {
                best_fd = fd;
                strncpy(best_name, name, sizeof(best_name) - 1);
                PLAYOS_LOG_I("input", "found gamepad (fallback): '%s' (%s)",
                             name, path);
            } else {
                PLAYOS_LOG_D("input", "ignoring additional gamepad: %s (%s)",
                             name, path);
                close(fd);
            }
        } else {
            skipped++;
            close(fd);
        }
    }

    closedir(dir);

    if (best_fd >= 0) {
        PLAYOS_LOG_I("input", "scan complete: selected '%s' (fd=%d) "
                     "out of %d devices (%d not gamepads)",
                     best_name, best_fd, scanned, skipped);
    } else {
        PLAYOS_LOG_W("input", "scan complete: NO gamepad found "
                     "(%d devices scanned, %d failed/skipped)",
                     scanned, skipped);
    }

    return best_fd;
}

/* ── Reserved-button device discovery ─────────────────────────────────── */

/* The ROG Ally exposes the Home button and the Armoury Crate / Command
 * Center / volume keys on evdev nodes that are separate from the main
 * gamepad node. These nodes report only the reserved keys, so they are
 * matched by capability bits rather than by name. */
static int is_reserved_home_device(int fd)
{
    unsigned long key_bits[EVDEV_BITS(KEY_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
        return 0;

    return TEST_BIT(BTN_MODE, key_bits) &&
           !TEST_BIT(BTN_SOUTH, key_bits);
}

static int is_reserved_vendor_device(int fd)
{
    unsigned long key_bits[EVDEV_BITS(KEY_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
        return 0;

    int has_reserved_keys =
        TEST_BIT(KEY_VOLUMEUP,        key_bits) ||
        TEST_BIT(KEY_VOLUMEDOWN,      key_bits) ||
        TEST_BIT(KEY_PROG1,           key_bits) ||
        TEST_BIT(KEY_PROG2,           key_bits) ||
        TEST_BIT(BTN_TRIGGER_HAPPY1,  key_bits) ||
        TEST_BIT(BTN_TRIGGER_HAPPY2,  key_bits);

    return has_reserved_keys &&
           !TEST_BIT(BTN_SOUTH, key_bits) &&
           !TEST_BIT(BTN_MODE,  key_bits);
}

/* The ROG Ally exposes the hardware power button as an ACPI "Power Button"
 * (and a separate "Sleep Button") evdev node. Both report KEY_POWER /
 * KEY_SLEEP and no gamepad buttons. The shell owns these: games must never
 * receive a power/suspend key, so we keep them on a dedicated reserved node. */
static int is_reserved_power_device(int fd)
{
    unsigned long key_bits[EVDEV_BITS(KEY_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
        return 0;

    return (TEST_BIT(KEY_POWER, key_bits) ||
            TEST_BIT(KEY_SLEEP, key_bits)) &&
           !TEST_BIT(BTN_SOUTH, key_bits) &&
           !TEST_BIT(BTN_MODE,  key_bits);
}

/* Append one reserved node fd to the shell's drain list. */
static void
shell_input_add_reserved_fd(struct playos_shell *s, int fd, const char *name)
{
    if (s->reserved_fd_count >= SHELL_MAX_RESERVED_FDS) {
        PLAYOS_LOG_W("input", "too many reserved nodes; dropping '%s'", name);
        close(fd);
        return;
    }

    s->reserved_fds[s->reserved_fd_count].fd = fd;
    snprintf(s->reserved_fds[s->reserved_fd_count].name,
             sizeof(s->reserved_fds[s->reserved_fd_count].name), "%s", name);
    s->reserved_fd_count++;
}

/* Open ALL reserved Asus/vendor/home evdev nodes, each exactly once.
 *
 * Sprint 9 input retest showed the ROG Ally exposes three "Asus Keyboard"
 * nodes (event6/7/8 in that build). The old per-role discovery opened
 * event8 twice — once as the vendor node and again as the volume node — and
 * never opened event6/event7, so KEY_VOLUMEUP/DOWN never reached the shell.
 * We now scan once, exclude the gamepad, and keep every node that is an Asus
 * node or matches the home/vendor capability matchers. Volume keys are then
 * decoded from whichever node actually emits them. */
static void
shell_input_open_reserved_nodes(struct playos_shell *s)
{
    PLAYOS_LOG_I("input", "scanning /dev/input/event* for reserved nodes...");

    s->reserved_fd_count = 0;

    DIR *dir = opendir("/dev/input");
    if (!dir) {
        PLAYOS_LOG_W("input", "cannot open /dev/input: %s", strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        char path[320];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        /* The main gamepad node must stay on s->evdev_fd only. */
        if (is_gamepad_device(fd)) {
            close(fd);
            continue;
        }

        char name[256] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);

        int asus   = strstr(name, "Asus") || strstr(name, "asus") ||
                     strstr(name, "ASUS");
        int home   = is_reserved_home_device(fd);
        int vendor = is_reserved_vendor_device(fd);
        int power  = is_reserved_power_device(fd);

        /* Priority: home → asus → vendor → power. Power comes last so the
         * Asus Keyboard node (vendor) and any Asus-named node keep their
         * existing roles; only genuine ACPI "Power Button"/"Sleep Button"
         * nodes fall through to the power role. */
        const char *role = NULL;
        if (home)
            role = "home";
        else if (asus)
            role = "asus";
        else if (vendor)
            role = "vendor";
        else if (power)
            role = "power";

        if (role) {
            PLAYOS_LOG_I("input", "opened reserved %s node: '%s' (%s) fd=%d",
                         role, name, path, fd);
            shell_input_add_reserved_fd(s, fd, role);
        } else {
            PLAYOS_LOG_D("input", "skipping non-reserved node '%s' (%s)",
                         name, path);
            close(fd);
        }
    }

    closedir(dir);

    if (s->reserved_fd_count == 0)
        PLAYOS_LOG_D("input", "no reserved nodes found");
}

/* One-time diagnostic: dump /proc/bus/input/devices to the persistent log so
 * a future log-only investigation can see the full kernel input topology even
 * without hardware access. Per-node names/phys/evbits are also logged by
 * shell_input_dump_capabilities() below. */
static void shell_input_dump_proc_devices(void)
{
    static int dumped = 0;
    if (dumped)
        return;
    dumped = 1;

    FILE *fp = fopen("/proc/bus/input/devices", "r");
    if (!fp) {
        PLAYOS_LOG_W("input", "cannot open /proc/bus/input/devices: %s",
                     strerror(errno));
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        PLAYOS_LOG_I("input", "proc-input: %s", line);
    }

    fclose(fp);
}

/* One-time diagnostic: dump every /dev/input/event* node's name, phys and the
 * reserved-key bits we care about. This correlates the shell's evdev view with
 * the kernel's dmesg input registrations and confirms exactly which node
 * carries volume / Command Center / Armoury keys. */
static void shell_input_dump_capabilities(void)
{
    static int dumped = 0;
    if (dumped)
        return;
    dumped = 1;

    DIR *dir = opendir("/dev/input");
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        char path[320];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        char name[256] = {0};
        char phys[256] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        ioctl(fd, EVIOCGPHYS(sizeof(phys) - 1), phys);

        unsigned long key_bits[EVDEV_BITS(KEY_MAX)] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) == 0) {
            struct {
                const char *label;
                unsigned int code;
            } interesting[] = {
                { "BTN_SOUTH",          BTN_SOUTH },
                { "BTN_MODE",           BTN_MODE },
                { "KEY_VOLUMEUP",       KEY_VOLUMEUP },
                { "KEY_VOLUMEDOWN",     KEY_VOLUMEDOWN },
                { "KEY_PROG1",          KEY_PROG1 },
                { "KEY_PROG2",          KEY_PROG2 },
                { "BTN_TRIGGER_HAPPY1", BTN_TRIGGER_HAPPY1 },
                { "BTN_TRIGGER_HAPPY2", BTN_TRIGGER_HAPPY2 },
                { "KEY_F15",            KEY_F15 },
                { "KEY_F16",            KEY_F16 },
                { "KEY_F17",            KEY_F17 },
                { "KEY_F18",            KEY_F18 },
            };

            char bits[256] = {0};
            size_t off = 0;
            for (size_t i = 0;
                 i < sizeof(interesting) / sizeof(interesting[0]); i++) {
                if (TEST_BIT(interesting[i].code, key_bits)) {
                    int n = snprintf(bits + off, sizeof(bits) - off,
                                     "%s%s", off ? "," : "",
                                     interesting[i].label);
                    if (n > 0 && (size_t)n < sizeof(bits) - off)
                        off += (size_t)n;
                    else
                        break;
                }
            }

            PLAYOS_LOG_I("input",
                         "input node %s name='%s' phys='%s' keys=[%s]",
                         path, name, phys, off ? bits : "(none)");
        }

        close(fd);
    }

    closedir(dir);
}

/* Normalize an analog trigger raw value into [0.0, 1.0]. */
static float
shell_input_normalize_trigger(int value, int min, int max, int calibrated)
{
    if (!calibrated || max <= min) {
        /* Unknown range: assume the common xpad-style 0..255 scale. */
        float v = (float)value / 255.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        return v;
    }

    float v = (float)(value - min) / (float)(max - min);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

/* Single source of truth for the shell's stick deadzone. Matches the
 * platform API backend (backend_evdev.c normalize_stick) so the shell and
 * games agree on where the center deadzone begins. No post-deadzone
 * rescale: the value snaps to 0 inside the band and is passed through
 * unchanged outside it, which keeps movement predictable in the Live Input
 * Test. */
#define SHELL_STICK_DEADZONE 0.05f

/* Normalize an analog stick raw value into [-1.0, 1.0].
 * Center is the midpoint of the calibrated range; Y up is negative (the
 * PlayOS convention). The dead zone is the fixed SHELL_STICK_DEADZONE. */
static float
shell_input_normalize_stick(const struct playos_shell *s, int axis, int value)
{
    int min, max;

    if (axis >= 0 && axis < 4 && s->stick_cal[axis].calibrated) {
        min = s->stick_cal[axis].min;
        max = s->stick_cal[axis].max;
    } else {
        /* Unknown range: assume the common xpad-style -32768..32767 scale. */
        min = -32768;
        max = 32767;
    }

    float center = (float)(min + max) * 0.5f;
    float half   = (float)(max - min) * 0.5f;
    if (half <= 0.0f)
        return 0.0f;

    float v = ((float)value - center) / half;
    if (v < -1.0f) v = -1.0f;
    if (v > 1.0f) v = 1.0f;

    if (v > -SHELL_STICK_DEADZONE && v < SHELL_STICK_DEADZONE)
        return 0.0f;

    return v;
}

/* Read ABS_Z / ABS_RZ ranges from the gamepad node. Trigger axes are
 * analog pedals, so the Live Input Test needs real min/max rather than
 * treating them as buttons. */
static void
shell_input_read_trigger_calibration(struct playos_shell *s)
{
    s->trigger_lt_calibrated = false;
    s->trigger_rt_calibrated = false;
    s->trigger_lt_min = s->trigger_lt_max = 0;
    s->trigger_rt_min = s->trigger_rt_max = 0;

    if (s->evdev_fd < 0)
        return;

    struct input_absinfo absinfo;

    if (ioctl(s->evdev_fd, EVIOCGABS(ABS_Z), &absinfo) == 0 &&
        absinfo.maximum > absinfo.minimum) {
        s->trigger_lt_min = absinfo.minimum;
        s->trigger_lt_max = absinfo.maximum;
        s->trigger_lt_calibrated = true;
    }

    if (ioctl(s->evdev_fd, EVIOCGABS(ABS_RZ), &absinfo) == 0 &&
        absinfo.maximum > absinfo.minimum) {
        s->trigger_rt_min = absinfo.minimum;
        s->trigger_rt_max = absinfo.maximum;
        s->trigger_rt_calibrated = true;
    }

    PLAYOS_LOG_I("input",
                 "trigger calibration LT[%d..%d]%s RT[%d..%d]%s",
                 s->trigger_lt_min, s->trigger_lt_max,
                 s->trigger_lt_calibrated ? "" : " (fallback 0..255)",
                 s->trigger_rt_min, s->trigger_rt_max,
                 s->trigger_rt_calibrated ? "" : " (fallback 0..255)");
}

/* Read ABS_X/Y/RX/RY ranges from the gamepad node. The Live Input Test
 * visualizes the sticks, so it needs the real min/max rather than assuming
 * the xpad-style -32768..32767 scale. flat is still captured and logged for
 * diagnostics, but normalization uses a fixed 5% dead zone (see
 * shell_input_normalize_stick). */
static void
shell_input_read_stick_calibration(struct playos_shell *s)
{
    static const struct {
        int  abs_code;
        int  axis;
        const char *label;
    } sticks[] = {
        { ABS_X,  PLAYOS_AXIS_LEFT_X,  "LX" },
        { ABS_Y,  PLAYOS_AXIS_LEFT_Y,  "LY" },
        { ABS_RX, PLAYOS_AXIS_RIGHT_X, "RX" },
        { ABS_RY, PLAYOS_AXIS_RIGHT_Y, "RY" },
    };

    for (size_t i = 0; i < sizeof(sticks) / sizeof(sticks[0]); i++) {
        s->stick_cal[sticks[i].axis].min = 0;
        s->stick_cal[sticks[i].axis].max = 0;
        s->stick_cal[sticks[i].axis].flat = 0;
        s->stick_cal[sticks[i].axis].calibrated = false;
    }

    if (s->evdev_fd < 0)
        return;

    struct input_absinfo absinfo;
    for (size_t i = 0; i < sizeof(sticks) / sizeof(sticks[0]); i++) {
        if (ioctl(s->evdev_fd, EVIOCGABS(sticks[i].abs_code), &absinfo) == 0 &&
            absinfo.maximum > absinfo.minimum) {
            s->stick_cal[sticks[i].axis].min = absinfo.minimum;
            s->stick_cal[sticks[i].axis].max = absinfo.maximum;
            s->stick_cal[sticks[i].axis].flat = absinfo.flat;
            s->stick_cal[sticks[i].axis].calibrated = true;
            PLAYOS_LOG_I("input", "stick calibration %s[%d..%d] flat=%d",
                         sticks[i].label, absinfo.minimum, absinfo.maximum,
                         absinfo.flat);
        } else {
            PLAYOS_LOG_I("input", "stick calibration %s (fallback -32768..32767)",
                         sticks[i].label);
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

int shell_input_init(struct playos_shell *s)
{
    shell_input_dump_proc_devices();
    shell_input_dump_capabilities();

    /* Watch /dev/input for CREATE/DELETE so a late-appearing gamepad (driver
     * load, USB/Bluetooth hotplug) is picked up immediately instead of waiting
     * out the poll-time retry throttle. Best-effort: input still works if the
     * watch cannot be installed. */
    s->input_inotify_fd = -1;
    s->input_inotify_wd = -1;
    s->input_inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (s->input_inotify_fd >= 0) {
        s->input_inotify_wd = inotify_add_watch(s->input_inotify_fd,
                                                "/dev/input",
                                                IN_CREATE | IN_DELETE | IN_ATTRIB);
        if (s->input_inotify_wd < 0) {
            PLAYOS_LOG_W("input", "cannot watch /dev/input: %s", strerror(errno));
            close(s->input_inotify_fd);
            s->input_inotify_fd = -1;
        }
    } else {
        PLAYOS_LOG_W("input", "inotify unavailable: %s", strerror(errno));
    }

    s->evdev_fd = find_gamepad_device();

    /* Reserved-button nodes are best-effort: they are separate evdev
     * streams on the ROG Ally and must not block normal input when absent.
     * Open every Asus/vendor/home node exactly once. */
    shell_input_open_reserved_nodes(s);

    s->trigger_lt_calibrated = false;
    s->trigger_rt_calibrated = false;
    s->trigger_lt_min = s->trigger_lt_max = 0;
    s->trigger_rt_min = s->trigger_rt_max = 0;

    s->raw_evdev_valid = false;
    s->raw_evdev_type = 0;
    s->raw_evdev_code = 0;
    s->raw_evdev_value = 0;
    s->raw_evdev_dev[0] = '\0';

    if (s->evdev_fd < 0) {
        PLAYOS_LOG_W("input", "no gamepad device found");
        return -1;
    }

    memset(&s->controller, 0, sizeof(s->controller));
    memset(&s->controller_prev, 0, sizeof(s->controller_prev));
    s->buttons_pressed = 0;

    shell_input_read_trigger_calibration(s);
    shell_input_read_stick_calibration(s);

    return 0;
}

/* Update a button bit and record a press edge.
 * ev.value semantics: 0 = release, 1 = press, 2 = auto-repeat.
 * Only a genuine press (value == 1) is an edge; auto-repeat is not. */
static void
input_apply_button(struct playos_shell *s, playos_button_mask_t mask, int value)
{
    if (value) {
        if (value == 1)
            s->buttons_pressed |= mask;
        s->controller.buttons |= mask;
    } else {
        s->controller.buttons &= ~mask;
    }
}

/* Hardware volume keys (vendor node). Step in 5% increments; act on any
 * non-zero value (including auto-repeat), ignore release. */
static void shell_input_volume_adjust(struct playos_shell *s, float delta)
{
    (void)s;

    PlayOSAudioInfo info;
    if (playos_audio_get_info(&info) != 0) {
        PLAYOS_LOG_W("input", "volume adjust: cannot read audio info");
        return;
    }

    float next = info.master_volume + delta;
    if (next < 0.0f) next = 0.0f;
    if (next > 1.0f) next = 1.0f;

    if (playos_audio_set_master_volume(next) == 0) {
        PLAYOS_LOG_D("input", "volume set to %.2f", next);
    } else {
        PLAYOS_LOG_W("input", "volume adjust denied (%.2f)", next);
    }
}

/* Decode a single evdev event into shell state.
 * Returns 1 on EV_SYN (end of one kernel frame), 0 otherwise. */
static int shell_input_process_event(struct playos_shell *s,
                                     const struct input_event *ev)
{
    switch (ev->type) {
    case EV_KEY:
        switch (ev->code) {
        /* ── Face buttons ── */
        case BTN_SOUTH:
            input_apply_button(s, PLAYOS_BUTTON_SOUTH, ev->value);
            break;
        case BTN_EAST:
            input_apply_button(s, PLAYOS_BUTTON_EAST, ev->value);
            break;
        case BTN_WEST:
            input_apply_button(s, PLAYOS_BUTTON_WEST, ev->value);
            break;
        case BTN_NORTH:
            input_apply_button(s, PLAYOS_BUTTON_NORTH, ev->value);
            break;

        /* ── Start / Select ── */
        case BTN_START:
            input_apply_button(s, PLAYOS_BUTTON_START, ev->value);
            break;
        case BTN_SELECT:
            input_apply_button(s, PLAYOS_BUTTON_SELECT, ev->value);
            break;

        /* ── Shoulder buttons / triggers ── */
        case BTN_TL:
            input_apply_button(s, PLAYOS_BUTTON_L1, ev->value);
            break;
        case BTN_TR:
            input_apply_button(s, PLAYOS_BUTTON_R1, ev->value);
            break;

        /* ── Stick clicks ── */
        case BTN_THUMBL:
            input_apply_button(s, PLAYOS_BUTTON_L3, ev->value);
            break;
        case BTN_THUMBR:
            input_apply_button(s, PLAYOS_BUTTON_R3, ev->value);
            break;

        /* ── D-pad as buttons (hid-asus and some drivers) ── */
        case BTN_DPAD_UP:
            input_apply_button(s, PLAYOS_BUTTON_DPAD_UP, ev->value);
            break;
        case BTN_DPAD_DOWN:
            input_apply_button(s, PLAYOS_BUTTON_DPAD_DOWN, ev->value);
            break;
        case BTN_DPAD_LEFT:
            input_apply_button(s, PLAYOS_BUTTON_DPAD_LEFT, ev->value);
            break;
        case BTN_DPAD_RIGHT:
            input_apply_button(s, PLAYOS_BUTTON_DPAD_RIGHT, ev->value);
            break;

        /* ── Reserved: SYSTEM (Xbox Guide / Armoury Crate) ── */
        case BTN_MODE:
        case KEY_PROG1:
        case BTN_TRIGGER_HAPPY1:
        case KEY_F17:  /* ROG Ally Armoury Crate (long-press, hid-asus) */
            input_apply_button(s, PLAYOS_BUTTON_SYSTEM, ev->value);
            break;
        case KEY_F18:  /* ROG Ally Armoury Crate long-press release marker */
            /* hid-asus emits F18 on release of the long-press. Treat any
             * non-zero F18 as the SYSTEM release so the button does not
             * stick. */
            if (ev->value) {
                s->controller.buttons &= ~PLAYOS_BUTTON_SYSTEM;
                s->buttons_pressed &= ~PLAYOS_BUTTON_SYSTEM;
            }
            break;

        /* ── Reserved: QUICK_MENU (Command Center / meta keys) ── */
        case KEY_F16:  /* ROG Ally Command Center (QAM, hid-asus) */
        case KEY_PROG2:
        case BTN_TRIGGER_HAPPY2:
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            input_apply_button(s, PLAYOS_BUTTON_QUICK_MENU, ev->value);
            break;

        /* ── Hardware volume keys (vendor node) ── */
        case KEY_VOLUMEUP:
            s->volume_up_held = (ev->value != 0);
            if (ev->value)
                shell_input_volume_adjust(s, 0.05f);
            break;
        case KEY_VOLUMEDOWN:
            s->volume_down_held = (ev->value != 0);
            if (ev->value)
                shell_input_volume_adjust(s, -0.05f);
            break;

        /* ── ROG Ally rear macro buttons (M1/M2) ──
         * Both rear macro buttons arrive as KEY_CUT (0x089) on this
         * hardware, so they are indistinguishable at the evdev level.
         * Tracked shell-local (not a public game button) for the Live
         * Input Test only. */
        case KEY_CUT:
            s->rear_macro_held = (ev->value != 0);
            break;

        /* ── Hardware power / sleep keys (ACPI power node) ── */
        case KEY_POWER:
        case KEY_SLEEP:
            input_apply_button(s, PLAYOS_BUTTON_POWER, ev->value);
            break;
        }
        break;

    case EV_ABS:
        /* Analog sticks are bidirectional axes (ABS_X/Y/RX/RY). */
        if (ev->code == ABS_X) {
            s->controller.axes[PLAYOS_AXIS_LEFT_X] =
                shell_input_normalize_stick(s, PLAYOS_AXIS_LEFT_X, ev->value);
        } else if (ev->code == ABS_Y) {
            s->controller.axes[PLAYOS_AXIS_LEFT_Y] =
                shell_input_normalize_stick(s, PLAYOS_AXIS_LEFT_Y, ev->value);
        } else if (ev->code == ABS_RX) {
            s->controller.axes[PLAYOS_AXIS_RIGHT_X] =
                shell_input_normalize_stick(s, PLAYOS_AXIS_RIGHT_X, ev->value);
        } else if (ev->code == ABS_RY) {
            s->controller.axes[PLAYOS_AXIS_RIGHT_Y] =
                shell_input_normalize_stick(s, PLAYOS_AXIS_RIGHT_Y, ev->value);
        }
        /* Analog triggers are pedals, not buttons. ABS_Z is left trigger
         * and ABS_RZ is right trigger on the ROG Ally / xpad mapping. */
        else if (ev->code == ABS_Z) {
            s->controller.axes[PLAYOS_AXIS_LEFT_TRIGGER] =
                shell_input_normalize_trigger(ev->value,
                                              s->trigger_lt_min,
                                              s->trigger_lt_max,
                                              s->trigger_lt_calibrated);
        } else if (ev->code == ABS_RZ) {
            s->controller.axes[PLAYOS_AXIS_RIGHT_TRIGGER] =
                shell_input_normalize_trigger(ev->value,
                                              s->trigger_rt_min,
                                              s->trigger_rt_max,
                                              s->trigger_rt_calibrated);
        }
        /* D-pad as ABS_HAT (xpad driver).
         * Mutually exclusive: LEFT clears RIGHT, UP clears DOWN. */
        else if (ev->code == ABS_HAT0X) {
            s->controller.buttons &= ~(PLAYOS_BUTTON_DPAD_LEFT |
                                       PLAYOS_BUTTON_DPAD_RIGHT);
            if (ev->value < 0)
                s->controller.buttons |= PLAYOS_BUTTON_DPAD_LEFT;
            else if (ev->value > 0)
                s->controller.buttons |= PLAYOS_BUTTON_DPAD_RIGHT;
        } else if (ev->code == ABS_HAT0Y) {
            s->controller.buttons &= ~(PLAYOS_BUTTON_DPAD_UP |
                                       PLAYOS_BUTTON_DPAD_DOWN);
            if (ev->value < 0)
                s->controller.buttons |= PLAYOS_BUTTON_DPAD_UP;
            else if (ev->value > 0)
                s->controller.buttons |= PLAYOS_BUTTON_DPAD_DOWN;
        }
        break;

    case EV_SYN:
        return 1;
    }

    return 0;
}

static void shell_input_drain_fd(struct playos_shell *s, int fd, const char *name)
{
    if (fd < 0)
        return;

    /* Raw-code diagnostics for the reserved nodes only. The ROG Ally's
     * Command Center / Armoury Crate buttons arrive as KEY_F16/F17/F18
     * rather than the PROG / TRIGGER_HAPPY codes the original discovery
     * expected, so tracing raw codes is how we confirm the mapping. Every
     * reserved node (home/asus/vendor) is logged; the gamepad is excluded
     * because sticks/triggers would flood the log with EV_ABS spam. */
    int raw_log = (name && name[0] && strcmp(name, "gamepad") != 0);

    /* Env-gated latency instrumentation. When PLAYOS_INPUT_LATENCY_LOG is set,
     * log (at most once per second) how old each drained evdev event is:
     * ev.time carries the kernel CLOCK_MONOTONIC timestamp at event
     * generation, so age = drain_time - event_time is queue-to-drain latency.
     * Off by default — per-event gettimeofday/clock_gettime is otherwise
     * wasted work on a 60 FPS shell. */
    static int latency_enabled = -1;
    static double last_latency_log = 0.0;
    if (latency_enabled < 0)
        latency_enabled = getenv("PLAYOS_INPUT_LATENCY_LOG") != NULL;

    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (latency_enabled == 1) {
            struct timespec now_ts;
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            double now_s = (double)now_ts.tv_sec + now_ts.tv_nsec * 1e-9;
            double ev_s = (double)ev.time.tv_sec + ev.time.tv_usec * 1e-6;
            double age_ms = (now_s - ev_s) * 1000.0;
            if (s->elapsed_time - last_latency_log >= 1.0) {
                PLAYOS_LOG_I("input", "latency %s age=%.3f ms (monotonic)",
                             name, age_ms);
                last_latency_log = s->elapsed_time;
            }
        }

        if (raw_log && ev.type == EV_KEY) {
            PLAYOS_LOG_D("input", "%s raw EV_KEY code=0x%x value=%d",
                         name, ev.code, ev.value);
        }

        /* Keep the latest non-SYN, non-zero event so the Live Input Test can
         * show the raw evdev code for ANY monitored input, not just decoded
         * gamepad buttons (covers volume keys, reserved keys, sticks,
         * triggers). Ignoring release (value == 0) keeps quick taps visible
         * on screen instead of being overwritten by the release next frame. */
        if (ev.type != EV_SYN && ev.value != 0) {
            s->raw_evdev_type = ev.type;
            s->raw_evdev_code = ev.code;
            s->raw_evdev_value = ev.value;
            if (name && name[0]) {
                snprintf(s->raw_evdev_dev, sizeof(s->raw_evdev_dev), "%s",
                         name);
            } else {
                s->raw_evdev_dev[0] = '\0';
            }
            s->raw_evdev_valid = true;
        }

        if (shell_input_process_event(s, &ev))
            break; /* Process one kernel frame per poll */
    }
}

/* A full /dev/input/event* scan (opendir + open + two ioctls per node) is far
 * too expensive to run every frame. Missing devices are retried at most once
 * per SHELL_INPUT_RESCAN_INTERVAL_SECONDS instead of on every poll, so a
 * permanently-absent node (for example no BTN_MODE-only home node on this
 * hardware) does not throttle the shell. */
#define SHELL_INPUT_RESCAN_INTERVAL_SECONDS 2.0

void shell_input_poll(struct playos_shell *s)
{
    /* Save previous state for edge detection */
    s->controller_prev = s->controller;
    s->buttons_pressed = 0;

    double now = s->elapsed_time;

    /* Hotplug: if /dev/input changed and we still have no gamepad, retry
     * discovery immediately instead of waiting out the 2s throttle. */
    if (s->input_inotify_fd >= 0) {
        char inotify_buf[4096];
        ssize_t n = read(s->input_inotify_fd, inotify_buf, sizeof(inotify_buf));
        if (n > 0 && s->evdev_fd < 0) {
            s->evdev_fd = find_gamepad_device();
            if (s->evdev_fd >= 0) {
                PLAYOS_LOG_I("input", "gamepad appeared (inotify) fd=%d",
                             s->evdev_fd);
                shell_input_read_trigger_calibration(s);
                shell_input_read_stick_calibration(s);
            }
        }
    }

    /* Auto-retry device discovery if fd is not open.
     * The controller device may appear later (driver load, hotplug). */
    if (s->evdev_fd < 0) {
        static double next_retry = 0.0;
        static int retry_count = 0;

        if (now >= next_retry) {
            s->evdev_fd = find_gamepad_device();
            next_retry = now + SHELL_INPUT_RESCAN_INTERVAL_SECONDS;

            if (s->evdev_fd >= 0) {
                PLAYOS_LOG_I("input", "gamepad appeared after retry (fd=%d)",
                             s->evdev_fd);
                shell_input_read_trigger_calibration(s);
                shell_input_read_stick_calibration(s);
            } else {
                retry_count++;
                /* First failure and every 15th (~30s) are worth logging. */
                if (retry_count == 1 || retry_count % 15 == 0) {
                    PLAYOS_LOG_W("input", "still no gamepad after %d retries",
                                 retry_count);
                }
            }
        }
    }

    /* Reserved-button nodes are discovered once in shell_input_init(). We
     * deliberately do NOT re-scan them here. A full /dev/input/event* scan
     * (opendir + open + two ioctls per node) costs ~0.5s on this hardware,
     * and repeating it every SHELL_INPUT_RESCAN_INTERVAL_SECONDS because a
     * BTN_MODE-only home node does not exist caused the visible input
     * hiccups. Hotplug of these reserved nodes is not expected during a
     * session; if it ever becomes a requirement, switch to inotify-based
     * discovery instead of polling. */

    if (s->evdev_fd < 0 && s->reserved_fd_count == 0)
        return;

    shell_input_drain_fd(s, s->evdev_fd, "gamepad");
    for (int i = 0; i < s->reserved_fd_count; i++) {
        shell_input_drain_fd(s, s->reserved_fds[i].fd,
                             s->reserved_fds[i].name);
    }
}

int shell_input_button_pressed(const struct playos_shell *s,
                               playos_button_mask_t button)
{
    int pressed = ((s->controller_prev.buttons & button) == 0) &&
                  ((s->controller.buttons & button) != 0);

    /* Also honor event-level edges: a press+release within one poll
     * (fast tap) never survives as a net state diff, so catch it here. */
    if (!pressed && (s->buttons_pressed & button))
        pressed = 1;

    /* Log button presses at most once per second to confirm input decoding.
     * This lets us trace whether the shell is reading the controller
     * correctly, independently of whether the screen logic reacts. */
    if (pressed) {
        static double last_log = 0.0;
        if (s->elapsed_time - last_log >= 1.0) {
            PLAYOS_LOG_D("input", "button 0x%08x pressed (buttons=0x%08x)",
                         button, s->controller.buttons);
            last_log = s->elapsed_time;
        }
    }

    return pressed;
}

int shell_input_button_released(const struct playos_shell *s,
                                playos_button_mask_t button)
{
    return ((s->controller_prev.buttons & button) != 0) &&
           ((s->controller.buttons & button) == 0);
}

/* Held query ("IsBeingPressed"): the button bit is currently set. This is the
 * level state — a fast tap that fully resolves within one poll still lights
 * the bit for that frame, but callers that need to catch short pulses should
 * use shell_input_button_pressed() for the edge. */
int shell_input_button_held(const struct playos_shell *s,
                            playos_button_mask_t button)
{
    return (s->controller.buttons & button) != 0;
}
