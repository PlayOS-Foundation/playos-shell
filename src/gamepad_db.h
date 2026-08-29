/*
 * gamepad_db.h — SDL_GameControllerDB-backed evdev remapping (Sprint 13.6)
 *
 * Internal to the evdev backend; not part of the public libplayos ABI.
 */
#ifndef PLAYOS_GAMEPAD_DB_H
#define PLAYOS_GAMEPAD_DB_H

#include <stddef.h>
#include <stdint.h>

/* Logical buttons we can map from a database entry (PlayOS order). */
enum {
    PLAYOS_DB_BTN_SOUTH = 0, /* A */
    PLAYOS_DB_BTN_EAST,      /* B */
    PLAYOS_DB_BTN_WEST,      /* X */
    PLAYOS_DB_BTN_NORTH,     /* Y */
    PLAYOS_DB_BTN_SELECT,    /* back */
    PLAYOS_DB_BTN_START,
    PLAYOS_DB_BTN_L3,
    PLAYOS_DB_BTN_R3,
    PLAYOS_DB_BTN_L1,
    PLAYOS_DB_BTN_R1,
    PLAYOS_DB_BTN_COUNT
};

/* Enumerated evdev tables for one device, in GLFW/SDL index order.
 * Button index = order of set bits from BTN_MISC upward.
 * Axis index   = order of set non-hat ABS bits from 0 upward.
 * Hat index    = order of hat pairs (ABS_HAT0X, ABS_HAT2X, ...). */
struct playos_db_evdev_tables {
    int key_by_index[512];   /* button index -> evdev code, -1 = none */
    int axis_by_index[64];   /* axis index -> evdev ABS code, -1 = none */
    int hat_x_by_index[4];   /* hat index -> ABS_HATnX code, -1 = none */
    int button_count;
    int axis_count;
    int hat_count;
};

/* Resolved per-device remap. -1 means "not available on this device". */
struct playos_db_remap {
    int buttons[PLAYOS_DB_BTN_COUNT];      /* evdev keycodes */
    int dpad_hat_x;                        /* ABS_HATnX code, or -1 */
    int dpad_hat_up, dpad_hat_right, dpad_hat_down, dpad_hat_left;
    int dpad_btn_up, dpad_btn_down, dpad_btn_left, dpad_btn_right;
    int abs_left_x, abs_left_y, abs_right_x, abs_right_y;
    int abs_left_trigger, abs_right_trigger;
};

/* Build the SDL GUID (32 lowercase hex chars) from EVIOCGID ids.
 * out must hold at least 33 bytes. */
void playos_db_guid_from_ids(uint16_t bustype, uint16_t vendor,
                             uint16_t product, uint16_t version,
                             char *out);

/* Enumerate the device's evdev tables. Returns 0 on success. */
int playos_db_build_tables(int fd, struct playos_db_evdev_tables *t);

/* Resolve the database entry matching the ids. Returns 0 and fills *out when
 * an entry matches, -1 otherwise (caller falls back to defaults). */
int playos_db_resolve(const struct playos_db_evdev_tables *t,
                      uint16_t bustype, uint16_t vendor,
                      uint16_t product, uint16_t version,
                      struct playos_db_remap *out);

/* Xbox-standard fallback (the pre-DB hardcoded table). */
void playos_db_default_remap(struct playos_db_remap *out);

/* Host-test accessors over the embedded database. */
size_t playos_db_line_count(void);
const char *playos_db_line(size_t i);

#endif /* PLAYOS_GAMEPAD_DB_H */
