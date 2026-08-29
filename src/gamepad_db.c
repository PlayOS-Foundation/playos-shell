/*
 * gamepad_db.c — SDL_GameControllerDB-backed evdev remapping (Sprint 13.6)
 *
 * Reproduces the GLFW/SDL Linux enumeration order so the community database's
 * `b#`/`a#`/`h#.#` indices resolve to the same evdev codes SDL would use.
 */
#include "gamepad_db.h"

#include <ctype.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "gamecontrollerdb.inc"

#define BIT_SET(bit, arr) (((arr)[(bit) / 8] >> ((bit) % 8)) & 1)

/* ── GUID ────────────────────────────────────────────────────────── */

void
playos_db_guid_from_ids(uint16_t bustype, uint16_t vendor,
                        uint16_t product, uint16_t version, char *out)
{
    snprintf(out, 33,
             "%02x%02x0000%02x%02x0000%02x%02x0000%02x%02x0000",
             bustype & 0xff, bustype >> 8,
             vendor & 0xff, vendor >> 8,
             product & 0xff, product >> 8,
             version & 0xff, version >> 8);
}

/* ── Enumeration (GLFW/SDL order) ────────────────────────────────── */

int
playos_db_build_tables(int fd, struct playos_db_evdev_tables *t)
{
    unsigned char key_bits[(KEY_MAX + 1 + 7) / 8];
    unsigned char abs_bits[(ABS_MAX + 1 + 7) / 8];

    memset(t, 0, sizeof(*t));
    for (int i = 0; i < 512; i++)
        t->key_by_index[i] = -1;
    for (int i = 0; i < 64; i++)
        t->axis_by_index[i] = -1;
    for (int i = 0; i < 4; i++)
        t->hat_x_by_index[i] = -1;

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
        return -1;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0)
        return -1;

    for (int code = BTN_MISC; code <= KEY_MAX; code++) {
        if (BIT_SET(code, key_bits))
            t->key_by_index[t->button_count++] = code;
    }

    for (int code = 0; code <= ABS_MAX; code++) {
        if (!BIT_SET(code, abs_bits))
            continue;
        if (code >= ABS_HAT0X && code <= ABS_HAT3Y) {
            if ((code - ABS_HAT0X) % 2 == 0)   /* X axis of the pair */
                t->hat_x_by_index[t->hat_count++] = code;
        } else {
            t->axis_by_index[t->axis_count++] = code;
        }
    }

    return 0;
}

/* ── Mapping line parsing ────────────────────────────────────────── */

/* Parsed SDL binding (indices are SDL enumeration indices; hats are packed
 * hat<<4 | bit with bit 1=up 2=right 4=down 8=left). */
struct db_binding {
    int btn[PLAYOS_DB_BTN_COUNT];
    int dpad_hat;   /* packed, or -1 */
    int dpad_btn[4]; /* up,right,down,left button indices, or -1 */
    int axis[6];     /* leftx,lefty,rightx,righty,lefttrig,righttrig */
};

/* Returns the SDL element type char ('a','b','h') of a mapping value,
 * skipping any leading +/-/~ modifiers. */
static char
sdl_type_char(const char *val)
{
    if (!val || !*val)
        return 0;
    if (*val == '+' || *val == '-' || *val == '~')
        val++;
    return *val;
}

static int
parse_sdl_index(const char *val, int *index_out)
{
    /* Accepts [b|a|h]N or [+|-|~][b|a|h]N (optional sign, then type). */
    if (!val || !*val)
        return -1;
    if (*val == '+' || *val == '-' || *val == '~')
        val++;
    if (*val == 'b' || *val == 'a' || *val == 'h')
        val++;
    char *end = NULL;
    long v = strtol(val, &end, 10);
    if (end == val)
        return -1;
    *index_out = (int)v;
    return 0;
}

static int
parse_binding(const char *line, struct db_binding *b)
{
    /* line: "<32-hex-guid>,<name>,<token>,<token>,..." */
    if (strlen(line) < 34 || line[32] != ',')
        return -1;

    for (int i = 0; i < PLAYOS_DB_BTN_COUNT; i++)
        b->btn[i] = -1;
    b->dpad_hat = -1;
    for (int i = 0; i < 4; i++)
        b->dpad_btn[i] = -1;
    for (int i = 0; i < 6; i++)
        b->axis[i] = -1;

    const char *tok = line + 33;   /* first character after GUID + comma */
    while (*tok) {
        const char *comma = strchr(tok, ',');
        size_t len = comma ? (size_t)(comma - tok) : strlen(tok);
        if (len == 0) {
            if (comma) { tok = comma + 1; continue; }
            break;
        }

        char field[32];
        if (len >= sizeof(field))
            len = sizeof(field) - 1;
        memcpy(field, tok, len);
        field[len] = '\0';

        const char *colon = strchr(field, ':');
        if (colon) {
            *((char *)colon) = '\0'; /* safe: field is our copy */
            const char *val = colon + 1;
            int idx = -1;

            if (strcmp(field, "a") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_SOUTH] = idx;
            } else if (strcmp(field, "b") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_EAST] = idx;
            } else if (strcmp(field, "x") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_WEST] = idx;
            } else if (strcmp(field, "y") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_NORTH] = idx;
            } else if (strcmp(field, "back") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_SELECT] = idx;
            } else if (strcmp(field, "start") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_START] = idx;
            } else if (strcmp(field, "leftstick") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_L3] = idx;
            } else if (strcmp(field, "rightstick") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_R3] = idx;
            } else if (strcmp(field, "leftshoulder") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_L1] = idx;
            } else if (strcmp(field, "rightshoulder") == 0) {
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'b' && idx >= 0)
                    b->btn[PLAYOS_DB_BTN_R1] = idx;
            } else if (strcmp(field, "dpup") == 0 || strcmp(field, "dpright") == 0 ||
                       strcmp(field, "dpdown") == 0 || strcmp(field, "dpleft") == 0) {
                int slot = strcmp(field, "dpup") == 0 ? 0 :
                           strcmp(field, "dpright") == 0 ? 1 :
                           strcmp(field, "dpdown") == 0 ? 2 : 3;
                if (sdl_type_char(val) == 'h') {
                    int hat = 0, bit = 0;
                    if (parse_sdl_index(val + 1, &hat) == 0) {
                        const char *dot = strchr(val + 1, '.');
                        if (dot && parse_sdl_index(dot + 1, &bit) == 0)
                            b->dpad_hat = (hat << 4) | bit;
                    }
                } else if (sdl_type_char(val) == 'b') {
                    if (parse_sdl_index(val, &idx) == 0 && idx >= 0)
                        b->dpad_btn[slot] = idx;
                }
            } else if (strcmp(field, "leftx") == 0 || strcmp(field, "lefty") == 0 ||
                       strcmp(field, "rightx") == 0 || strcmp(field, "righty") == 0 ||
                       strcmp(field, "lefttrigger") == 0 || strcmp(field, "righttrigger") == 0) {
                int slot = strcmp(field, "leftx") == 0 ? 0 :
                           strcmp(field, "lefty") == 0 ? 1 :
                           strcmp(field, "rightx") == 0 ? 2 :
                           strcmp(field, "righty") == 0 ? 3 :
                           strcmp(field, "lefttrigger") == 0 ? 4 : 5;
                if (parse_sdl_index(val, &idx) == 0 && sdl_type_char(val) == 'a' && idx >= 0)
                    b->axis[slot] = idx;
            }
            /* guide / misc1 / paddle-star / touchpad: ignored for now */
        }

        if (!comma)
            break;
        tok = comma + 1;
    }
    return 0;
}

/* ── Resolution ──────────────────────────────────────────────────── */

static int
axis_code(const struct playos_db_evdev_tables *t, int idx)
{
    if (idx < 0 || idx >= t->axis_count)
        return -1;
    return t->axis_by_index[idx];
}

static int
button_code(const struct playos_db_evdev_tables *t, int idx)
{
    if (idx < 0 || idx >= t->button_count)
        return -1;
    return t->key_by_index[idx];
}

int
playos_db_resolve(const struct playos_db_evdev_tables *t,
                  uint16_t bustype, uint16_t vendor,
                  uint16_t product, uint16_t version,
                  struct playos_db_remap *out)
{
    char guid[33];
    playos_db_guid_from_ids(bustype, vendor, product, version, guid);

    for (size_t i = 0; i < kGameControllerDBCount; i++) {
        const char *line = kGameControllerDB[i];
        if (strncmp(line, guid, 32) != 0)
            continue;

        struct db_binding b;
        if (parse_binding(line, &b) != 0)
            continue;

        memset(out, 0, sizeof(*out));
        for (int k = 0; k < PLAYOS_DB_BTN_COUNT; k++)
            out->buttons[k] = -1;
        out->dpad_hat_x = -1;
        out->dpad_hat_up = out->dpad_hat_right = out->dpad_hat_down = out->dpad_hat_left = 0;
        out->dpad_btn_up = out->dpad_btn_down = out->dpad_btn_left = out->dpad_btn_right = -1;
        out->abs_left_x = out->abs_left_y = out->abs_right_x = out->abs_right_y = -1;
        out->abs_left_trigger = out->abs_right_trigger = -1;

        for (int k = 0; k < PLAYOS_DB_BTN_COUNT; k++)
            out->buttons[k] = button_code(t, b.btn[k]);

        if (b.dpad_hat >= 0) {
            int hat = b.dpad_hat >> 4;
            int bits = b.dpad_hat & 0xf;
            if (hat < t->hat_count) {
                out->dpad_hat_x = t->hat_x_by_index[hat];
                if (bits & 1) out->dpad_hat_up = 1;
                if (bits & 2) out->dpad_hat_right = 1;
                if (bits & 4) out->dpad_hat_down = 1;
                if (bits & 8) out->dpad_hat_left = 1;
            }
        } else {
            out->dpad_btn_up = button_code(t, b.dpad_btn[0]);
            out->dpad_btn_right = button_code(t, b.dpad_btn[1]);
            out->dpad_btn_down = button_code(t, b.dpad_btn[2]);
            out->dpad_btn_left = button_code(t, b.dpad_btn[3]);
        }

        out->abs_left_x = axis_code(t, b.axis[0]);
        out->abs_left_y = axis_code(t, b.axis[1]);
        out->abs_right_x = axis_code(t, b.axis[2]);
        out->abs_right_y = axis_code(t, b.axis[3]);
        out->abs_left_trigger = axis_code(t, b.axis[4]);
        out->abs_right_trigger = axis_code(t, b.axis[5]);
        return 0;
    }

    return -1;
}

void
playos_db_default_remap(struct playos_db_remap *out)
{
    memset(out, 0, sizeof(*out));
    out->buttons[PLAYOS_DB_BTN_SOUTH]  = BTN_SOUTH;
    out->buttons[PLAYOS_DB_BTN_EAST]   = BTN_EAST;
    out->buttons[PLAYOS_DB_BTN_WEST]   = BTN_WEST;
    out->buttons[PLAYOS_DB_BTN_NORTH]  = BTN_NORTH;
    out->buttons[PLAYOS_DB_BTN_SELECT] = BTN_SELECT;
    out->buttons[PLAYOS_DB_BTN_START]  = BTN_START;
    out->buttons[PLAYOS_DB_BTN_L3]     = BTN_THUMBL;
    out->buttons[PLAYOS_DB_BTN_R3]     = BTN_THUMBR;
    out->buttons[PLAYOS_DB_BTN_L1]     = BTN_TL;
    out->buttons[PLAYOS_DB_BTN_R1]     = BTN_TR;
    out->dpad_hat_x = ABS_HAT0X;
    out->dpad_hat_up = 1;
    out->dpad_hat_right = 2;
    out->dpad_hat_down = 4;
    out->dpad_hat_left = 8;
    out->dpad_btn_up = out->dpad_btn_down = out->dpad_btn_left = out->dpad_btn_right = -1;
    out->abs_left_x = ABS_X;
    out->abs_left_y = ABS_Y;
    out->abs_right_x = ABS_RX;
    out->abs_right_y = ABS_RY;
    out->abs_left_trigger = ABS_Z;
    out->abs_right_trigger = ABS_RZ;
}

size_t
playos_db_line_count(void)
{
    return kGameControllerDBCount;
}

const char *
playos_db_line(size_t i)
{
    if (i >= kGameControllerDBCount)
        return NULL;
    return kGameControllerDB[i];
}
