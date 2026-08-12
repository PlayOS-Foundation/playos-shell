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

/* ── Central shell state ─────────────────────────────────────────────── */

struct playos_shell {
    /* ── Screens ── */
    enum playos_screen current_screen;
    enum playos_screen previous_screen;
    bool               running;
    bool               configured;

    /* ── Input (evdev — trusted, keeps SYSTEM/QUICK_MENU) ── */
    int  evdev_fd;
    PlayOSControllerState controller;
    PlayOSControllerState controller_prev;  /* For edge detection */

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
    int    selected_game_index;

    /* ── Lifecycle ── */
    bool   is_suspended;

    /* ── Settings cursor ── */
    int    settings_tab;          /* 0=display, 1=audio, 2=power, 3=system */
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

/* ── Raylib PlayOS backend accessor (defined in rcore_playos.c) ──────── */

struct playos_manager_v1 *platform_get_playos_manager(void);

/* ── Render utilities (defined in render_util.c) ─────────────────────── */

void render_draw_rect(float x, float y, float w, float h,
                      float r, float g, float b, float a);
void render_draw_text(const char *text, float x, float y,
                      float scale, float r, float g, float b, float a);
void render_begin_frame(float r, float g, float b, float a);
void render_end_frame(struct playos_shell *s);
void render_screen_dims(int *w, int *h);
float render_text_width(const char *text, float scale);

#endif /* PLAYOS_SHELL_H */
