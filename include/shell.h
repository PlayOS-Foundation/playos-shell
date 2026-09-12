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
    SCREEN_RECOVERY,
    SCREEN_INSTALLER,     /* S14-T10: app-style install front-end */
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
    int  gamepad_face_swap;    /* ROG Ally quirk: swap WEST<->NORTH on the
                                  internal controller (X/Y wired rotated) */
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
    bool   rear_macro_pressed;   /* Event-level press edge this poll (fast tap) */

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
    bool   game_running;   /* a game is launched and has not yet exited */

    /* ── Power / thermal status (Sprint 9) ── */
    PlayOSPowerInfo  power_info;          /* Cached battery/temp/profile state */
    bool             power_info_valid;    /* power_info has been filled */
    struct timespec  last_status_refresh; /* Monotonic time of last refresh */

    /* ── Display brightness (Sprint 9.5) ── */
    int              display_brightness;        /* 0..100, -1 when unavailable */
    bool             display_brightness_valid;  /* display_brightness filled */

    /* ── Settings cursor ── */
    int    settings_tab;            /* active tab (see TAB_* enum in screen_settings.c) */
    int    recovery_mode;           /* 1 = launched with PLAYOS_RECOVERY=1 */
    int    recovery_cursor;         /* active recovery menu item */
    int    recovery_confirm;        /* 1 = confirm modal active */
    int    recovery_log_view;       /* 1 = showing /data/log file list */
    int    recovery_log_cursor;     /* selected log file index */
    int    recovery_log_content;    /* 1 = viewing a log file's contents */
    int    recovery_log_count;      /* number of log files listed */
    char   recovery_log_files[24][96];
    char   recovery_log_path[256];  /* currently open log file */
    float  settings_tab_scroll;     /* horizontal tab-bar scroll offset (px) */
    float  settings_content_scroll; /* vertical content scroll offset (px) */
    /* ── Selectable rows (System tab) ── */
    int    settings_power_cursor;   /* 0 = Screenshot, 1 = Power Off, 2 = Restart,
                                       3 = Check for Update, 4 = Apply Update,
                                       5 = Restart to Apply */
    bool   power_confirm;           /* confirmation dialog active */

    /* ── Screenshot (COMMAND reserved button) ── */
    bool   screenshot_enabled;      /* capture on reserved button when true */
    bool   screenshot_pending;      /* one-frame request to capture */
    bool   screenshot_ok;           /* last capture result */
    double screenshot_flash_until;  /* elapsed_time until toast hides */
    double screenshot_debounce_until; /* ignore repeat presses until then */

    /* ── Software update (Sprint 11) ── */
    char   update_bundle_path[512]; /* selected *.playosb from /data/updates */
    bool   update_bundle_found;     /* a single update bundle was found */
    bool   update_in_progress;      /* ApplyUpdate accepted; events streaming */
    bool   update_ready;            /* UpdateComplete received; reboot to apply */
    int    update_percent;          /* coarse 0..100 progress (IPC gives none) */
    char   update_step[64];         /* human-readable progress label */
    char   boot_slot[16];           /* boot.json active slot 'a'/'b' or unknown */
    char   boot_slot_health[16];    /* active slot health, or "unknown" */
    char   boot_slot_version[64];   /* active slot version, or "unknown" */
    bool   update_restart_confirm;  /* "Restart to apply update?" modal active */
    bool   install_payload_present; /* S13.7: playos-a payload found on boot medium */

    /* ── Installer front-end (S14-T10) ──────────────────────────────────
     * The shell shows the app-styled disk picker and the destructive confirm;
     * init then hands the chosen disk to the installer (PLAYOS_INSTALL_TARGET)
     * so the destructive phase starts without asking again. */
#define SHELL_INSTALLER_MAX_DISKS 8
    char   installer_path[SHELL_INSTALLER_MAX_DISKS][80];   /* /dev/nvme0n1 */
    char   installer_label[SHELL_INSTALLER_MAX_DISKS][96];  /* model + size  */
    int    installer_count;
    int    installer_cursor;
    bool   installer_confirm;       /* hold-A confirmation stage active */
    double installer_hold_start;    /* elapsed_time the A hold began, 0 = idle */

    /* ── Transient toast (Sprint 11) ── */
    char   toast_msg[256];          /* message shown while toast_until active */
    double toast_until;             /* elapsed_time until toast hides */

    /* ── (ipc_fd removed — per-call connect/launch/disconnect pattern) ── */
};

/* ── Lifecycle (defined in main.c) ──────────────────────────────────── */

void shell_handle_lifecycle(struct playos_shell *s);

/* ── Transient toast + boot slot (defined in main.c) ─────────────────── */

void shell_set_toast(struct playos_shell *s, const char *msg);
void shell_refresh_boot_slot(struct playos_shell *s);

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

/* S14-T6: recovery menu shown when launched with PLAYOS_RECOVERY=1. */
void screen_recovery_enter(struct playos_shell *s);
void screen_recovery_update(struct playos_shell *s);
void screen_recovery_draw(struct playos_shell *s);

/* S14-T10: app-style installer front-end (disk picker + destructive confirm)
 * launched from Settings, handing the chosen disk to the runtime installer. */
void screen_installer_enter(struct playos_shell *s);
void screen_installer_update(struct playos_shell *s);
void screen_installer_draw(struct playos_shell *s);

/* ── Full-output capture (defined in screencopy.c) ───────────────────── */

/* Capture the composited output (game, overlay and shell together) into a
 * PNG at `path` using zwlr_screencopy_manager_v1. Returns 1 on success and 0
 * when screencopy is unavailable or the copy failed; the caller then falls
 * back to a shell-surface grab. */
int shell_capture_output(const char *path);

/* Persisted "screenshot buttons" preference (defined in main.c). */
void shell_screenshot_setting_load(struct playos_shell *s);
void shell_screenshot_setting_save(const struct playos_shell *s);

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
void render_draw_text_gradient(const char *text, float x, float y, float scale,
                               float bottom_r, float bottom_g, float bottom_b,
                               float top_r, float top_g, float top_b);
void render_begin_frame(float r, float g, float b, float a);
void render_end_frame(struct playos_shell *s);
void render_screen_dims(int *w, int *h);
float render_text_width(const char *text, float scale);
void render_font_init(void);

#endif /* PLAYOS_SHELL_H */
