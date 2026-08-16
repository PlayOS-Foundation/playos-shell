/**
 * shell.h — PlayOS Shell central state and screen definitions
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PLAYOS_SHELL_H
#define PLAYOS_SHELL_H

#include <stdbool.h>
#include <time.h>

#include "playos/playos_input.h"
#include "playos/playos_power.h"

/* Forward declaration: bound by the Raylib PlayOS backend (rcore_playos.c)
 * and consumed by main.c for the trusted-shell registration handshake. */
struct playos_manager_v1;

/* ── Screen enum ─────────────────────────────────────────────────────── */

enum playos_screen {
    SCREEN_HOME,
    SCREEN_LIBRARY,
    SCREEN_GAME_DETAIL,
    SCREEN_SETTINGS,
};

/* ── Reserved-button evdev nodes ────────────────────────────────────────
 * The ROG Ally splits reserved keys (Home, Command Center, Armoury Crate,
 * volume) across several "Asus Keyboard" event nodes. Open all of them (each
 * exactly once) and drain them independently, instead of a single fd per role
 * that can double-open the same node and miss the real volume node. */
#define SHELL_MAX_RESERVED_FDS 8

struct shell_reserved_fd {
    int  fd;
    char name[16];   /* short diagnostic label: home/vendor/volume/asus */
};

/* ── Central shell state ─────────────────────────────────────────────── */

struct playos_shell {
    /* ── Screens ── */
    enum playos_screen current_screen;
    enum playos_screen previous_screen;
    bool               running;
    bool               configured;

    /* ── Home menu cursor ── */
    int    home_cursor;           /* 0 = Library, 1 = Settings */

    /* ── Input (evdev — trusted, keeps SYSTEM/QUICK_MENU) ── */
    int  evdev_fd;             /* Main gamepad node (face buttons, sticks) */
    int  input_inotify_fd;     /* inotify watch on /dev/input (hotplug) */
    int  input_inotify_wd;     /* watch descriptor for /dev/input */
    struct shell_reserved_fd reserved_fds[SHELL_MAX_RESERVED_FDS];
    int  reserved_fd_count;    /* Number of valid entries in reserved_fds[] */
    PlayOSControllerState controller;
    PlayOSControllerState controller_prev;  /* For edge detection */
    playos_button_mask_t   buttons_pressed; /* Event-level press edges this poll.
                                               Catches press+release within one
                                               frame (fast taps) that the net
                                               state diff would otherwise drop. */
    bool   volume_up_held;       /* vendor KEY_VOLUMEUP currently held */
    bool   volume_down_held;     /* vendor KEY_VOLUMEDOWN currently held */
    bool   rear_macro_held;      /* ROG Ally rear macro M1/M2 (KEY_CUT, shared) */

    /* ── Analog trigger calibration (evdev ABS_Z / ABS_RZ) ── */
    int    trigger_lt_min, trigger_lt_max;   /* Left trigger raw range */
    int    trigger_rt_min, trigger_rt_max;   /* Right trigger raw range */
    bool   trigger_lt_calibrated;
    bool   trigger_rt_calibrated;

    /* ── Analog stick calibration (evdev ABS_X/Y/RX/RY) ──
     * Indexed directly by PLAYOS_AXIS_LEFT_X..PLAYOS_AXIS_RIGHT_Y (0..3). */
    struct {
        int  min;
        int  max;
        int  flat;         /* evdev deadzone, in raw units */
        bool calibrated;
    } stick_cal[4];

    /* ── Raw evdev diagnostic (Live Input Test) ── */
    uint16_t raw_evdev_type;      /* Latest non-SYN event type */
    uint16_t raw_evdev_code;      /* Latest non-SYN event code */
    int32_t  raw_evdev_value;     /* Latest non-SYN event value */
    char     raw_evdev_dev[16];   /* Which monitored node delivered it */
    bool     raw_evdev_valid;

    /* ── Output ── */
    int    output_width;
    int    output_height;
    float  dpi_scale;

    /* ── Timing ── */
    struct timespec start_time;
    double          frame_time;
    double          elapsed_time;

    /* ── Game library ── */
    int    game_count;
    char   game_ids[64][128];         /* Up to 64 games, 128-char IDs */
    char   game_names[64][128];       /* Display names from manifest.json */
    char   game_versions[64][64];     /* Versions from manifest.json */
    char   game_descriptions[64][256];/* Descriptions from manifest.json */
    bool   game_has_icon[64];         /* assets/icon.png exists and loaded */
    int    selected_game_index;

    /* ── Lifecycle ── */
    bool   is_suspended;

    /* ── Power / thermal status (Sprint 9) ── */
    PlayOSPowerInfo  power_info;          /* Cached battery/temp/profile state */
    bool             power_info_valid;    /* power_info has been filled */
    struct timespec  last_status_refresh; /* Monotonic time of last refresh */

    /* ── Settings cursor ── */
    int    settings_tab;            /* active tab (see TAB_* enum in screen_settings.c) */
    float  settings_tab_scroll;     /* horizontal tab-bar scroll offset (px) */
    float  settings_content_scroll; /* vertical content scroll offset (px) */
    /* ── Power actions (System tab) ── */
    int    settings_power_cursor;   /* 0 = Power Off, 1 = Restart */
    bool   power_confirm;           /* confirmation dialog active */
    /* ── (ipc_fd removed — per-call connect/launch/disconnect pattern) ── */
};

/* ── Lifecycle (defined in main.c) ──────────────────────────────────── */

void shell_handle_lifecycle(struct playos_shell *s);

/* ── Screen functions (defined in screen_*.c) ────────────────────────── */

void screen_home_enter(struct playos_shell *s);
void screen_home_update(struct playos_shell *s);
void screen_home_draw(struct playos_shell *s);

void screen_library_enter(struct playos_shell *s);
void screen_library_update(struct playos_shell *s);
void screen_library_draw(struct playos_shell *s);

void screen_game_detail_enter(struct playos_shell *s);
void screen_game_detail_update(struct playos_shell *s);
void screen_game_detail_draw(struct playos_shell *s);

void screen_settings_enter(struct playos_shell *s);
void screen_settings_update(struct playos_shell *s);
void screen_settings_draw(struct playos_shell *s);

/* ── Input (defined in input.c) ──────────────────────────────────────── */

int  shell_input_init(struct playos_shell *s);
void shell_input_poll(struct playos_shell *s);
int  shell_input_button_pressed(const struct playos_shell *s,
                                playos_button_mask_t button);
int  shell_input_button_released(const struct playos_shell *s,
                                 playos_button_mask_t button);
int  shell_input_button_held(const struct playos_shell *s,
                             playos_button_mask_t button);

/* ── Raylib PlayOS backend accessor (defined in rcore_playos.c) ──────── */

struct playos_manager_v1 *platform_get_playos_manager(void);

/* ── Render utilities (defined in render_util.c) ─────────────────────── */

void render_draw_rect(float x, float y, float w, float h,
                      float r, float g, float b, float a);
void render_draw_triangle(float x1, float y1, float x2, float y2,
                          float x3, float y3,
                          float r, float g, float b, float a);
void render_draw_circle(float cx, float cy, float radius,
                        float r, float g, float b, float a);
void render_draw_circle_lines(float cx, float cy, float radius,
                              float r, float g, float b, float a);
void render_begin_scissor(int x, int y, int w, int h);
void render_end_scissor(void);
void render_draw_text(const char *text, float x, float y,
                      float scale, float r, float g, float b, float a);
void render_begin_frame(float r, float g, float b, float a);
void render_end_frame(struct playos_shell *s);
void render_screen_dims(int *w, int *h);
float render_text_width(const char *text, float scale);
void render_font_init(void);

#endif /* PLAYOS_SHELL_H */
