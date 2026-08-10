/**
 * screen_game_detail.c — Game detail / launch screen for PlayOS Shell
 *
 * Shows the selected game's title and a "Launch" option.
 * Sends LaunchGame to playos-init via trusted IPC on /run/playos/control.sock.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shell.h"

#include <stdio.h>
#include <string.h>

#ifdef PLAYOS_TRUSTED_IPC
#include "playos-runtime/trusted_control.h"
#endif

/* ── Enter ─────────────────────────────────────────────────────────────── */

void
screen_game_detail_enter(struct playos_shell *s)
{
    fprintf(stderr, "[I] shell game_detail: viewing game '%s'\n",
            s->game_ids[s->selected_game_index]);
}

/* ── Update ────────────────────────────────────────────────────────────── */

void
screen_game_detail_update(struct playos_shell *s)
{
    /* A: Launch */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
        const char *game_id = s->game_ids[s->selected_game_index];
#ifdef PLAYOS_TRUSTED_IPC
        fprintf(stderr, "[I] shell game_detail: LAUNCH '%s' via IPC\n", game_id);
        int ret = playos_trusted_launch_game(s->ipc_fd, game_id);
        if (ret == 0) {
            fprintf(stderr, "[I] shell game_detail: launch accepted\n");
        } else {
            fprintf(stderr, "[E] shell game_detail: launch failed for '%s'\n", game_id);
        }
#else
        fprintf(stderr, "[I] shell game_detail: LAUNCH requested for '%s' "
                "(IPC not available)\n", game_id);
#endif
        /* Stay on this screen */
    }

    /* B: back to library */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
        s->current_screen = SCREEN_LIBRARY;
        screen_library_enter(s);
        return;
    }
}

/* ── Draw ──────────────────────────────────────────────────────────────── */

void
screen_game_detail_draw(struct playos_shell *s)
{
    int w = s->output_width;
    int h = s->output_height;

    /* ── Background ── */
    render_begin_frame(0.06f, 0.12f, 0.22f, 1.0f);

    float title_scale = (float)h / 45.0f;
    float sub_scale   = title_scale * 0.4f;
    float hint_scale  = sub_scale * 0.8f;

    /* ── Game title ── */
    const char *game_id = s->game_ids[s->selected_game_index];
    float title_w = render_text_width(game_id, title_scale);
    float title_x = ((float)w - title_w) * 0.5f;
    float title_y = (float)h * 0.10f;

    render_draw_text(game_id, title_x, title_y, title_scale,
                     1.0f, 1.0f, 1.0f, 1.0f);

    /* ── Icon placeholder ── */
    float icon_size = 160.0f;
    float icon_x = ((float)w - icon_size) * 0.5f;
    float icon_y = title_y + title_scale * 14.0f;

    render_draw_rect(icon_x, icon_y, icon_size, icon_size,
                     0.15f, 0.25f, 0.40f, 1.0f);

    float q_scale = 5.0f;
    const char *q = "?";
    float q_w = render_text_width(q, q_scale);
    render_draw_text(q,
                     icon_x + (icon_size - q_w) * 0.5f,
                     icon_y + icon_size * 0.35f,
                     q_scale, 0.5f, 0.5f, 0.5f, 1.0f);

    /* ── Description placeholder ── */
    float desc_y = icon_y + icon_size + 20.0f;
    const char *desc = "No description available.";
    float desc_w = render_text_width(desc, sub_scale);
    render_draw_text(desc, ((float)w - desc_w) * 0.5f, desc_y,
                     sub_scale, 0.5f, 0.5f, 0.6f, 1.0f);

    /* ── Launch hint ── */
    float launch_y = (float)h * 0.65f;
    const char *launch = "Press [A] to Launch";
    float launch_w = render_text_width(launch, sub_scale);
    render_draw_text(launch, ((float)w - launch_w) * 0.5f, launch_y,
                     sub_scale, 0.84f, 0.42f, 0.0f, 1.0f);

    /* ── Navigation hint ── */
    const char *back = "[B] Back to Library";
    float back_w = render_text_width(back, hint_scale);
    render_draw_text(back, ((float)w - back_w) * 0.5f,
                     (float)h - hint_scale * 20.0f,
                     hint_scale, 0.5f, 0.5f, 0.6f, 1.0f);
}
