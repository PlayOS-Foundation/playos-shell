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
 * Xbox controllers always have ABS_X, ABS_Y, ABS_RX, ABS_RY,
 * and BTN_SOUTH/BTN_EAST/etc.
 * D-pad may be ABS_HAT0X/Y (Xbox) or BTN_DPAD_* (some drivers).
 * Accept either form. */
static int is_gamepad_device(int fd)
{
    unsigned long abs_bits[EVDEV_BITS(ABS_MAX)] = {0};
    unsigned long key_bits[EVDEV_BITS(KEY_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0)
        return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
        return 0;

    /* Must have sticks */
    if (!TEST_BIT(ABS_X, abs_bits) || !TEST_BIT(ABS_Y, abs_bits))
        return 0;

    /* Must have at least the face buttons */
    if (!TEST_BIT(BTN_SOUTH, key_bits))
        return 0;

    /* D-pad: accept ABS_HAT or BTN_DPAD_* */
    int has_hat = TEST_BIT(ABS_HAT0X, abs_bits) || TEST_BIT(ABS_HAT0Y, abs_bits);
    int has_dpad_btns = TEST_BIT(BTN_DPAD_UP, key_bits) || TEST_BIT(BTN_DPAD_DOWN, key_bits);
    if (!has_hat && !has_dpad_btns)
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
                strstr(name, "ASUE") ||        /* ASUS ROG Ally (all models) */
                strstr(name, "ASUS") ||
                strstr(name, "ROG Ally") ||
                strstr(name, "Gamepad")) {     /* Catch "ASUE* Gamepad" etc. */
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
    /* Save previous state for edge detection */
    s->controller_prev = s->controller;

    /* Auto-retry device discovery if fd is not open.
     * The controller device may appear later (driver load, hotplug). */
    if (s->evdev_fd < 0) {
        s->evdev_fd = find_gamepad_device();
    }

    if (s->evdev_fd < 0)
        return;

    struct input_event ev;
    ssize_t n;

    while ((n = read(s->evdev_fd, &ev, sizeof(ev))) == sizeof(ev)) {
        switch (ev.type) {
        case EV_KEY:
            switch (ev.code) {
            /* ── Face buttons ── */
            case BTN_SOUTH:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_SOUTH;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_SOUTH;
                break;
            case BTN_EAST:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_EAST;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_EAST;
                break;
            case BTN_WEST:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_WEST;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_WEST;
                break;
            case BTN_NORTH:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_NORTH;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_NORTH;
                break;

            /* ── Start / Select ── */
            case BTN_START:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_START;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_START;
                break;
            case BTN_SELECT:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_SELECT;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_SELECT;
                break;

            /* ── Shoulder buttons / triggers ── */
            case BTN_TL:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_L1;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_L1;
                break;
            case BTN_TR:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_R1;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_R1;
                break;

            /* ── Stick clicks ── */
            case BTN_THUMBL:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_L3;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_L3;
                break;
            case BTN_THUMBR:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_R3;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_R3;
                break;

            /* ── D-pad as buttons (hid-asus and some drivers) ── */
            case BTN_DPAD_UP:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_DPAD_UP;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_DPAD_UP;
                break;
            case BTN_DPAD_DOWN:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_DPAD_DOWN;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_DPAD_DOWN;
                break;
            case BTN_DPAD_LEFT:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_DPAD_LEFT;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_DPAD_LEFT;
                break;
            case BTN_DPAD_RIGHT:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_DPAD_RIGHT;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_DPAD_RIGHT;
                break;

            /* ── Reserved: SYSTEM (Xbox Guide) ── */
            case BTN_MODE:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_SYSTEM;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_SYSTEM;
                break;

            /* ── Reserved: QUICK_MENU (Ally Armoury Crate / CC) ── */
            case KEY_PROG1:
            case KEY_PROG2:
            case KEY_LEFTMETA:
            case KEY_RIGHTMETA:
            case BTN_TRIGGER_HAPPY1:
                if (ev.value) s->controller.buttons |= PLAYOS_BUTTON_QUICK_MENU;
                else          s->controller.buttons &= ~PLAYOS_BUTTON_QUICK_MENU;
                break;
            }
            break;

        case EV_ABS:
            /* D-pad as ABS_HAT (xpad driver).
             * Mutually exclusive: LEFT clears RIGHT, UP clears DOWN. */
            if (ev.code == ABS_HAT0X) {
                s->controller.buttons &= ~(PLAYOS_BUTTON_DPAD_LEFT |
                                           PLAYOS_BUTTON_DPAD_RIGHT);
                if (ev.value < 0)
                    s->controller.buttons |= PLAYOS_BUTTON_DPAD_LEFT;
                else if (ev.value > 0)
                    s->controller.buttons |= PLAYOS_BUTTON_DPAD_RIGHT;
            } else if (ev.code == ABS_HAT0Y) {
                s->controller.buttons &= ~(PLAYOS_BUTTON_DPAD_UP |
                                           PLAYOS_BUTTON_DPAD_DOWN);
                if (ev.value < 0)
                    s->controller.buttons |= PLAYOS_BUTTON_DPAD_UP;
                else if (ev.value > 0)
                    s->controller.buttons |= PLAYOS_BUTTON_DPAD_DOWN;
            }
            break;

        case EV_SYN:
            goto done; /* Process one frame's worth of events */
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
