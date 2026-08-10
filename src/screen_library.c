/**
 * screen_library.c — Game library browser for PlayOS Shell
 *
 * Scans /data/games for installed titles, renders a controller-navigable
 * grid with selection highlighting.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shell.h"
#include "playos/playos_storage.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* ── Grid layout constants ─────────────────────────────────────────────── */

#define GRID_COLS       4
#define GRID_ROWS       4
#define CARD_PADDING    16
#define CARD_WIDTH      280
#define CARD_HEIGHT     200
#define GRID_TOP_MARGIN 180

/* ── Enter: scan games directory ──────────────────────────────────────── */

void
screen_library_enter(struct playos_shell *s)
{
    s->game_count = 0;
    s->selected_game_index = 0;

    const char *games_path = playos_storage_get_games_path();
    if (!games_path) {
        fprintf(stderr, "[W] shell library: playos_storage_get_games_path() "
                "returned NULL — no games directory available\n");
        return;
    }

    DIR *dir = opendir(games_path);
    if (!dir) {
        fprintf(stderr, "[W] shell library: cannot open %s\n", games_path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s->game_count < 64) {
        /* Skip dotfiles */
        if (entry->d_name[0] == '.')
            continue;

        /* Only count directories */
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", games_path,
                 entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        /* Store game ID */
        strncpy(s->game_ids[s->game_count], entry->d_name,
                sizeof(s->game_ids[0]) - 1);
        s->game_ids[s->game_count][sizeof(s->game_ids[0]) - 1] = '\0';
        s->game_count++;
    }

    closedir(dir);

    fprintf(stderr, "[I] shell library: found %d games in %s\n",
            s->game_count, games_path);
}

/* ── Update: controller navigation ────────────────────────────────────── */

void
screen_library_update(struct playos_shell *s)
{
    if (s->game_count == 0) {
        /* No games: B returns home */
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
            s->current_screen = SCREEN_HOME;
            screen_home_enter(s);
        }
        return;
    }

    int col = s->selected_game_index % GRID_COLS;
    int row = s->selected_game_index / GRID_COLS;

    /* D-pad navigation */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_LEFT)) {
        if (col > 0)
            s->selected_game_index--;
    }
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_RIGHT)) {
        if (col < GRID_COLS - 1 &&
            s->selected_game_index + 1 < s->game_count)
            s->selected_game_index++;
    }
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP)) {
        if (row > 0)
            s->selected_game_index -= GRID_COLS;
    }
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN)) {
        if (row < GRID_ROWS - 1 &&
            s->selected_game_index + GRID_COLS < s->game_count)
            s->selected_game_index += GRID_COLS;
    }

    /* A: game detail */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
        s->current_screen = SCREEN_GAME_DETAIL;
        screen_game_detail_enter(s);
        return;
    }

    /* B: back to home */
    if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
        s->current_screen = SCREEN_HOME;
        screen_home_enter(s);
        return;
    }
}

/* ── Draw: game card grid ─────────────────────────────────────────────── */

static void
draw_game_card(struct playos_shell *s, int index,
               float x, float y, int is_selected)
{
    float card_w = (float)CARD_WIDTH;
    float card_h = (float)CARD_HEIGHT;

    /* Card background */
    if (is_selected) {
        /* Selected highlight */
        float glow = 0.6f + 0.4f * sinf((float)s->elapsed_time * 3.0f);
        render_draw_rect(x - 3.0f, y - 3.0f, card_w + 6.0f, card_h + 6.0f,
                         0.84f, 0.42f, 0.0f, glow);
    }

    render_draw_rect(x, y, card_w, card_h,
                     0.12f, 0.20f, 0.35f, 1.0f);

    /* Icon placeholder (top-center) */
    float icon_size = 80.0f;
    float icon_x = x + (card_w - icon_size) * 0.5f;
    float icon_y = y + 12.0f;

    /* Darker placeholder rect */
    render_draw_rect(icon_x, icon_y, icon_size, icon_size,
                     0.15f, 0.25f, 0.40f, 1.0f);

    /* "?" in placeholder */
    float q_scale = 4.0f;
    const char *q = "?";
    float q_w = render_text_width(q, q_scale);
    render_draw_text(q,
                     icon_x + (icon_size - q_w) * 0.5f,
                     icon_y + icon_size * 0.35f,
                     q_scale, 0.5f, 0.5f, 0.5f, 1.0f);

    /* Game title (truncated) */
    const char *game_id = s->game_ids[index];
    float title_scale = 2.2f;
    float title_w = render_text_width(game_id, title_scale);
    float title_max_w = card_w - 16.0f;

    if (title_w > title_max_w) {
        /* Truncate with "..." — simple approach: reduce scale */
        title_scale *= title_max_w / title_w;
        title_w = render_text_width(game_id, title_scale);
    }

    render_draw_text(game_id,
                     x + (card_w - title_w) * 0.5f,
                     icon_y + icon_size + 12.0f,
                     title_scale,
                     is_selected ? 1.0f : 0.8f,
                     is_selected ? 1.0f : 0.8f,
                     is_selected ? 1.0f : 0.8f,
                     1.0f);
}

void
screen_library_draw(struct playos_shell *s)
{
    int w = s->output_width;
    int h = s->output_height;

    /* ── Background ── */
    render_begin_frame(0.06f, 0.12f, 0.22f, 1.0f);

    /* ── Header ── */
    float header_scale = (float)h / 50.0f;
    const char *header = "Library";
    float header_w = render_text_width(header, header_scale);
    render_draw_text(header, ((float)w - header_w) * 0.5f,
                     20.0f, header_scale, 1.0f, 1.0f, 1.0f, 1.0f);

    /* ── Game count ── */
    char count_text[64];
    snprintf(count_text, sizeof(count_text), "%d game(s) installed",
             s->game_count);
    float count_scale = header_scale * 0.35f;
    float count_w = render_text_width(count_text, count_scale);
    render_draw_text(count_text, ((float)w - count_w) * 0.5f,
                     20.0f + header_scale * 12.0f, count_scale,
                     0.6f, 0.6f, 0.7f, 1.0f);

    /* ── Empty state ── */
    if (s->game_count == 0) {
        float empty_scale = header_scale * 0.4f;
        /* Center the first line */
        const char *line1 = "No games installed.";
        float l1_w = render_text_width(line1, empty_scale);
        const char *line2 = "Place game directories in /data/games/";
        float l2_w = render_text_width(line2, empty_scale);
        float center_y = (float)h * 0.45f;
        render_draw_text(line1, ((float)w - l1_w) * 0.5f, center_y,
                         empty_scale, 0.6f, 0.6f, 0.6f, 1.0f);
        render_draw_text(line2, ((float)w - l2_w) * 0.5f,
                         center_y + empty_scale * 14.0f,
                         empty_scale, 0.4f, 0.4f, 0.5f, 1.0f);
    }

    /* ── Game grid ── */
    if (s->game_count > 0) {
        /* Center the grid horizontally */
        float grid_total_w = (float)(GRID_COLS * CARD_WIDTH +
                                     (GRID_COLS - 1) * CARD_PADDING);
        float grid_start_x = ((float)w - grid_total_w) * 0.5f;

        for (int i = 0; i < s->game_count; i++) {
            int col = i % GRID_COLS;
            int row = i / GRID_COLS;
            float cx = grid_start_x + (float)col * (float)(CARD_WIDTH + CARD_PADDING);
            float cy = (float)GRID_TOP_MARGIN +
                       (float)row * (float)(CARD_HEIGHT + CARD_PADDING);

            draw_game_card(s, i, cx, cy, i == s->selected_game_index);
        }
    }

    /* ── Navigation hints ── */
    float hint_scale = header_scale * 0.25f;
    const char *hints = "[A] Select    [B] Back    [D-Pad] Navigate";
    float hints_w = render_text_width(hints, hint_scale);
    render_draw_text(hints, ((float)w - hints_w) * 0.5f,
                     (float)h - hint_scale * 20.0f,
                     hint_scale, 0.5f, 0.5f, 0.6f, 1.0f);
}
