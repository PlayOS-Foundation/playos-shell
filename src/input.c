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

static int find_reserved_device(const char *label, int (*match)(int))
{
    PLAYOS_LOG_I("input", "scanning /dev/input/event* for %s...", label);

    DIR *dir = opendir("/dev/input");
    if (!dir) {
        PLAYOS_LOG_W("input", "cannot open /dev/input: %s", strerror(errno));
        return -1;
    }

    int found_fd = -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        char path[320];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        if (match(fd)) {
            char name[256] = {0};
            ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
            PLAYOS_LOG_I("input", "found %s: '%s' (%s) fd=%d",
                         label, name, path, fd);
            found_fd = fd;
            break;
        }

        close(fd);
    }

    closedir(dir);

    if (found_fd < 0)
        PLAYOS_LOG_D("input", "no %s device found", label);

    return found_fd;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int shell_input_init(struct playos_shell *s)
{
    s->evdev_fd = find_gamepad_device();

    /* Reserved-button nodes are best-effort: they are separate evdev
     * streams on the ROG Ally and must not block normal input when absent. */
    s->evdev_home_fd = find_reserved_device("home node", is_reserved_home_device);
    s->evdev_vendor_fd = find_reserved_device("vendor node",
                                              is_reserved_vendor_device);

    if (s->evdev_fd < 0) {
        PLAYOS_LOG_W("input", "no gamepad device found");
        return -1;
    }

    memset(&s->controller, 0, sizeof(s->controller));
    memset(&s->controller_prev, 0, sizeof(s->controller_prev));
    s->buttons_pressed = 0;

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
            input_apply_button(s, PLAYOS_BUTTON_SYSTEM, ev->value);
            break;

        /* ── Reserved: QUICK_MENU (Command Center / meta keys) ── */
        case KEY_PROG2:
        case BTN_TRIGGER_HAPPY2:
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            input_apply_button(s, PLAYOS_BUTTON_QUICK_MENU, ev->value);
            break;

        /* ── Hardware volume keys (vendor node) ── */
        case KEY_VOLUMEUP:
            if (ev->value)
                shell_input_volume_adjust(s, 0.05f);
            break;
        case KEY_VOLUMEDOWN:
            if (ev->value)
                shell_input_volume_adjust(s, -0.05f);
            break;
        }
        break;

    case EV_ABS:
        /* D-pad as ABS_HAT (xpad driver).
         * Mutually exclusive: LEFT clears RIGHT, UP clears DOWN. */
        if (ev->code == ABS_HAT0X) {
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

static void shell_input_drain_fd(struct playos_shell *s, int fd)
{
    if (fd < 0)
        return;

    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
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

    if (s->evdev_fd < 0 && s->evdev_home_fd < 0 && s->evdev_vendor_fd < 0)
        return;

    shell_input_drain_fd(s, s->evdev_fd);
    shell_input_drain_fd(s, s->evdev_home_fd);
    shell_input_drain_fd(s, s->evdev_vendor_fd);
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
