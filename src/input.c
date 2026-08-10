/**
 * input.c — Direct evdev controller input (trusted, keeps reserved buttons)
 *
 * The shell reads controller input directly from /dev/input/event* because
 * libplayos' input API strips PLAYOS_BUTTON_SYSTEM and PLAYOS_BUTTON_QUICK_MENU
 * from game processes. The shell is trusted and needs those buttons for
 * quick-menu (overlay) and home-button functionality.
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

/* Quick check: does this event device have the gamepad axes + buttons?
 * Xbox controllers always have ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_HAT0X,
 * and BTN_SOUTH/BTN_EAST/etc. */
static int is_gamepad_device(int fd)
{
    unsigned long abs_bits[EVDEV_BITS(ABS_MAX)] = {0};
    unsigned long key_bits[EVDEV_BITS(KEY_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0)
        return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
        return 0;

    /* Must have sticks and dpad */
    if (!TEST_BIT(ABS_X, abs_bits) || !TEST_BIT(ABS_Y, abs_bits))
        return 0;
    if (!TEST_BIT(ABS_HAT0X, abs_bits) && !TEST_BIT(ABS_HAT0Y, abs_bits))
        return 0;

    /* Must have at least the face buttons */
    if (!TEST_BIT(BTN_SOUTH, key_bits))
        return 0;

    return 1;
}

static int find_gamepad_device(void)
{
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        PLAYOS_LOG_W("input", "cannot open /dev/input: %s", strerror(errno));
        return -1;
    }

    int best_fd = -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        char path[320];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        if (is_gamepad_device(fd)) {
            /* Check for Xbox/ASUS gamepad by name */
            char name[256] = {0};
            ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);

            /* Prefer Xbox/Ally controllers over generic HID */
            if (strstr(name, "Xbox") || strstr(name, "xbox") ||
                strstr(name, "X-Box") ||
                strstr(name, "Microsoft") ||
                strstr(name, "ASUE") ||    /* ASUS ROG Ally */
                strstr(name, "ROG Ally")) {
                PLAYOS_LOG_I("input", "found gamepad: %s (%s)", name, path);
                closedir(dir);
                return fd;
            }

            /* Keep the first viable fallback */
            if (best_fd < 0) {
                best_fd = fd;
                PLAYOS_LOG_I("input", "found gamepad (fallback): %s (%s)",
                             name, path);
            } else {
                close(fd);
            }
        } else {
            close(fd);
        }
    }

    closedir(dir);
    return best_fd;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int shell_input_init(struct playos_shell *s)
{
    s->evdev_fd = find_gamepad_device();
    if (s->evdev_fd < 0) {
        PLAYOS_LOG_W("input", "no gamepad device found");
        return -1;
    }

    memset(&s->controller, 0, sizeof(s->controller));
    memset(&s->controller_prev, 0, sizeof(s->controller_prev));

    return 0;
}

void shell_input_poll(struct playos_shell *s)
{
    if (s->evdev_fd < 0)
        return;

    /* Save previous state for edge detection */
    s->controller_prev = s->controller;

    /* Get standard controller state from Platform API (Sprint 5).
     * This provides all buttons (except SYSTEM/QUICK_MENU which are
     * stripped by the API) and all axes with proper dead-zone / range
     * normalization, rather than using ROG Ally-specific raw evdev codes. */
    playos_input_get_controller_state(&s->controller);

    /* Read raw evdev ONLY for reserved buttons the Platform API strips.
     * The shell is trusted and needs SYSTEM (Xbox/Guide) and QUICK_MENU
     * (Ally Armoury Crate / CC button) for overlay and home functionality. */
    struct input_event ev;
    ssize_t n;

    while ((n = read(s->evdev_fd, &ev, sizeof(ev))) == sizeof(ev)) {
        switch (ev.type) {
        case EV_KEY:
            /* Xbox/Guide button → PLAYOS_BUTTON_SYSTEM */
            if (ev.code == BTN_MODE) {
                if (ev.value)
                    s->controller.buttons |= PLAYOS_BUTTON_SYSTEM;
                else
                    s->controller.buttons &= ~PLAYOS_BUTTON_SYSTEM;
            }
            /* Ally quick-menu (Armoury Crate) and other reserved key paths */
            else if (ev.code == KEY_PROG1 || ev.code == KEY_PROG2 ||
                     ev.code == KEY_LEFTMETA || ev.code == KEY_RIGHTMETA ||
                     ev.code == BTN_TRIGGER_HAPPY1) {
                if (ev.value)
                    s->controller.buttons |= PLAYOS_BUTTON_QUICK_MENU;
                else
                    s->controller.buttons &= ~PLAYOS_BUTTON_QUICK_MENU;
            }
            break;
        case EV_SYN:
            goto done; /* Process one frame's worth of reserved-button events */
        }
    }
done:
    (void)0;
}

int shell_input_button_pressed(const struct playos_shell *s,
                               playos_button_mask_t button)
{
    return ((s->controller_prev.buttons & button) == 0) &&
           ((s->controller.buttons & button) != 0);
}

int shell_input_button_released(const struct playos_shell *s,
                                playos_button_mask_t button)
{
    return ((s->controller_prev.buttons & button) != 0) &&
           ((s->controller.buttons & button) == 0);
}
