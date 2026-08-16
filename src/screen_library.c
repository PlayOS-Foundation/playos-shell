/**
 * screen_library.c — Game library browser for PlayOS Shell
 *
 * Scans /data/games for installed titles, validates each game manifest,
 * renders a controller-navigable grid with selection highlighting, and
 * shows per-game icon art when assets/icon.png is present.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shell.h"
#include "playos/playos.h"
#include "playos/playos_storage.h"
#include "playos/playos_logging.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Per-game icon textures (GPU side) ─────────────────────────────────── */
static Texture2D s_game_icons[64];
static bool     s_game_icons_loaded[64];

/* ── Minimal JSON string extractor ──────────────────────────────────────
 * Extracts a quoted string value for a given key from a simple JSON object.
 * Handles the constrained format used by PlayOS game manifests.
 * Returns the number of characters written to out (excluding null), or 0. */
static int
json_get_string(const char *json, const char *key,
                char *out, size_t out_sz)
{
    if (!json || !key || !out || out_sz == 0)
        return 0;

    /* Search for "key" */
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *pos = strstr(json, search);
    if (!pos)
        return 0;

    /* Move past "key": */
    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\n')
        pos++;

    if (*pos != '"')
        return 0;
    pos++; /* Skip opening quote */

    /* Copy until closing quote */
    size_t i = 0;
    while (*pos && *pos != '"' && i < out_sz - 1) {
        /* Handle simple escape sequences */
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            switch (*pos) {
            case '"':  out[i++] = '"';  break;
            case '\\': out[i++] = '\\'; break;
            case 'n':  out[i++] = '\n'; break;
            case 't':  out[i++] = '\t'; break;
            default:   out[i++] = *pos; break;
            }
        } else {
            out[i++] = *pos;
        }
        pos++;
    }
    out[i] = '\0';
    return (int)i;
}

/* Minimal JSON integer extractor for game manifests. Returns 1 on success. */
static int
json_get_int(const char *json, const char *key, int *out)
{
    if (!json || !key || !out)
        return 0;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *pos = strstr(json, search);
    if (!pos)
        return 0;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\n')
        pos++;

    if (!(*pos == '-' || (*pos >= '0' && *pos <= '9')))
        return 0;

    char *end = NULL;
    long v = strtol(pos, &end, 10);
    if (end == pos)
        return 0;

    *out = (int)v;
    return 1;
}

/* Architecture of the host binary the shell was compiled for. */
static const char *
host_architecture(void)
{
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#else
    return "unknown";
#endif
}

/* Read entire file into a malloc'd buffer. Caller must free. */
static char *
read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 65536) { /* 64KB sanity limit */
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* Validate a game directory's manifest against the current host and the
 * supported API version, then populate the shell game arrays at index.
 * Returns 1 when the game is valid and was loaded, 0 when it was skipped. */
static int
validate_and_load_manifest(struct playos_shell *s, int index,
                           const char *games_path, const char *dir_name)
{
    char manifest_path[640];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s/%s/manifest.json", games_path, dir_name);

    char *json = read_file(manifest_path);
    if (!json) {
        PLAYOS_LOG_W("shell", "library: skip %s (cannot read manifest)",
                     dir_name);
        return 0;
    }

    char id[256]         = {0};
    char name[256]       = {0};
    char version[256]    = {0};
    char executable[256] = {0};
    char architecture[64] = {0};
    int  api_version     = 0;

    int ok = 1;
    if (json_get_string(json, "id", id, sizeof(id)) <= 0) {
        PLAYOS_LOG_W("shell", "library: skip %s (missing id)", dir_name);
        ok = 0;
    }
    if (json_get_string(json, "name", name, sizeof(name)) <= 0) {
        PLAYOS_LOG_W("shell", "library: skip %s (missing name)", dir_name);
        ok = 0;
    }
    if (json_get_string(json, "version", version, sizeof(version)) <= 0) {
        PLAYOS_LOG_W("shell", "library: skip %s (missing version)", dir_name);
        ok = 0;
    }
    if (json_get_string(json, "executable", executable, sizeof(executable)) <= 0) {
        PLAYOS_LOG_W("shell", "library: skip %s (missing executable)", dir_name);
        ok = 0;
    }
    if (json_get_string(json, "architecture", architecture,
                        sizeof(architecture)) <= 0) {
        PLAYOS_LOG_W("shell", "library: skip %s (missing architecture)", dir_name);
        ok = 0;
    }
    if (!json_get_int(json, "api_version", &api_version)) {
        PLAYOS_LOG_W("shell", "library: skip %s (missing api_version)", dir_name);
        ok = 0;
    }

    if (!ok) {
        free(json);
        return 0;
    }

    /* The manifest id must match the directory name. */
    if (strcmp(id, dir_name) != 0) {
        PLAYOS_LOG_W("shell", "library: skip %s (id mismatch: %s)",
                     dir_name, id);
        free(json);
        return 0;
    }

    /* api_version must be supported by this shell build. */
    if (api_version < 1 || api_version > PLAYOS_API_VERSION) {
        PLAYOS_LOG_W("shell", "library: skip %s (unsupported api_version %d)",
                     dir_name, api_version);
        free(json);
        return 0;
    }

    /* Architecture must match the running host. */
    if (strcmp(architecture, host_architecture()) != 0) {
        PLAYOS_LOG_W("shell", "library: skip %s (architecture %s != %s)",
                     dir_name, architecture, host_architecture());
        free(json);
        return 0;
    }

    /* The referenced executable must exist inside the game directory. */
    char exe_path[640];
    snprintf(exe_path, sizeof(exe_path), "%s/%s/%s",
             games_path, dir_name, executable);
    if (access(exe_path, F_OK) != 0) {
        PLAYOS_LOG_W("shell", "library: skip %s (executable missing: %s)",
                     dir_name, executable);
        free(json);
        return 0;
    }

    /* Valid — populate the shell arrays. */
    strncpy(s->game_ids[index], id, sizeof(s->game_ids[0]) - 1);
    s->game_ids[index][sizeof(s->game_ids[0]) - 1] = '\0';

    strncpy(s->game_names[index], name, sizeof(s->game_names[0]) - 1);
    s->game_names[index][sizeof(s->game_names[0]) - 1] = '\0';

    strncpy(s->game_versions[index], version, sizeof(s->game_versions[0]) - 1);
    s->game_versions[index][sizeof(s->game_versions[0]) - 1] = '\0';

    char description[256] = {0};
    json_get_string(json, "description", description, sizeof(description));
    strncpy(s->game_descriptions[index], description,
            sizeof(s->game_descriptions[0]) - 1);
    s->game_descriptions[index][sizeof(s->game_descriptions[0]) - 1] = '\0';

    char icon_path[640];
    snprintf(icon_path, sizeof(icon_path), "%s/%s/assets/icon.png",
             games_path, dir_name);
    s->game_has_icon[index] = (access(icon_path, F_OK) == 0);

    free(json);
    return 1;
}

/* Swap two game entries (all text fields and the icon flag). */
static void
swap_games(struct playos_shell *s, int a, int b)
{
    char buf128[128];
    char buf64[64];
    char buf256[256];

    memcpy(buf128, s->game_ids[a], sizeof(buf128));
    memcpy(s->game_ids[a], s->game_ids[b], sizeof(buf128));
    memcpy(s->game_ids[b], buf128, sizeof(buf128));

    memcpy(buf128, s->game_names[a], sizeof(buf128));
    memcpy(s->game_names[a], s->game_names[b], sizeof(buf128));
    memcpy(s->game_names[b], buf128, sizeof(buf128));

    memcpy(buf64, s->game_versions[a], sizeof(buf64));
    memcpy(s->game_versions[a], s->game_versions[b], sizeof(buf64));
    memcpy(s->game_versions[b], buf64, sizeof(buf64));

    memcpy(buf256, s->game_descriptions[a], sizeof(buf256));
    memcpy(s->game_descriptions[a], s->game_descriptions[b], sizeof(buf256));
    memcpy(s->game_descriptions[b], buf256, sizeof(buf256));

    bool has_icon = s->game_has_icon[a];
    s->game_has_icon[a] = s->game_has_icon[b];
    s->game_has_icon[b] = has_icon;
}

/* Stable insertion sort by display name (case-sensitive). */
static void
sort_games(struct playos_shell *s)
{
    for (int i = 1; i < s->game_count; i++) {
        for (int j = i; j > 0; j--) {
            if (strcmp(s->game_names[j - 1], s->game_names[j]) > 0)
                swap_games(s, j - 1, j);
            else
                break;
        }
    }
}

static void
library_unload_icons(void)
{
    for (int i = 0; i < 64; i++) {
        if (s_game_icons_loaded[i]) {
            UnloadTexture(s_game_icons[i]);
            s_game_icons_loaded[i] = false;
        }
    }
}

static void
library_load_icons(struct playos_shell *s, const char *games_path)
{
    library_unload_icons();

    for (int i = 0; i < s->game_count; i++) {
        s_game_icons_loaded[i] = false;
        if (!s->game_has_icon[i])
            continue;

        char icon_path[640];
        snprintf(icon_path, sizeof(icon_path),
                 "%s/%s/assets/icon.png", games_path, s->game_ids[i]);

        Texture2D tex = LoadTexture(icon_path);
        if (tex.id != 0) {
            s_game_icons[i] = tex;
            s_game_icons_loaded[i] = true;
        } else {
            PLAYOS_LOG_W("shell", "library: failed to load icon %s", icon_path);
            s->game_has_icon[i] = false;
        }
    }
}

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

    /* Clear manifest data arrays */
    memset(s->game_ids, 0, sizeof(s->game_ids));
    memset(s->game_names, 0, sizeof(s->game_names));
    memset(s->game_versions, 0, sizeof(s->game_versions));
    memset(s->game_descriptions, 0, sizeof(s->game_descriptions));
    memset(s->game_has_icon, 0, sizeof(s->game_has_icon));

    library_unload_icons();

    const char *games_path = playos_storage_get_games_path();
    if (!games_path) {
        PLAYOS_LOG_W("shell", "library: get_games_path() returned NULL");
        return;
    }

    DIR *dir = opendir(games_path);
    if (!dir) {
        PLAYOS_LOG_W("shell", "library: cannot open %s", games_path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
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

        if (s->game_count >= 64) {
            PLAYOS_LOG_W("shell", "library: game limit reached; ignoring %s",
                         entry->d_name);
            continue;
        }

        /* Only a validated manifest advances the count. */
        if (validate_and_load_manifest(s, s->game_count, games_path,
                                       entry->d_name))
            s->game_count++;
    }

    closedir(dir);

    sort_games(s);
    library_load_icons(s, games_path);

    PLAYOS_LOG_I("shell", "library: found %d games in %s",
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

    /* Icon (top-center) */
    float icon_size = 80.0f;
    float icon_x = x + (card_w - icon_size) * 0.5f;
    float icon_y = y + 12.0f;

    if (s->game_has_icon[index] && s_game_icons_loaded[index]) {
        Texture2D *tex = &s_game_icons[index];
        Rectangle src = { 0.0f, 0.0f, (float)tex->width, (float)tex->height };
        Rectangle dst = { icon_x, icon_y, icon_size, icon_size };
        DrawTexturePro(*tex, src, dst, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    } else {
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
    }

    /* Game title (use display name from manifest, fallback to game ID) */
    const char *display_name = s->game_names[index];
    if (!display_name[0])
        display_name = s->game_ids[index];
    float title_scale = 2.2f;
    float title_w = render_text_width(display_name, title_scale);
    float title_max_w = card_w - 16.0f;

    if (title_w > title_max_w) {
        /* Truncate with "..." — simple approach: reduce scale */
        title_scale *= title_max_w / title_w;
        title_w = render_text_width(display_name, title_scale);
    }

    render_draw_text(display_name,
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
    float header_scale = (float)h / 240.0f * s->dpi_scale;
    const char *header = "Library";
    float header_w = render_text_width(header, header_scale);
    render_draw_text(header, ((float)w - header_w) * 0.5f,
                     20.0f, header_scale, 1.0f, 1.0f, 1.0f, 1.0f);

    /* ── Game count ── */
    char count_text[64];
    snprintf(count_text, sizeof(count_text), "%d game(s) installed",
             s->game_count);
    float count_scale = header_scale * 0.5f;
    float count_w = render_text_width(count_text, count_scale);
    render_draw_text(count_text, ((float)w - count_w) * 0.5f,
                     20.0f + header_scale * 12.0f, count_scale,
                     0.6f, 0.6f, 0.7f, 1.0f);

    /* ── Empty state ── */
    if (s->game_count == 0) {
        float empty_scale = header_scale * 0.55f;
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
    float hint_scale = header_scale * 0.45f;
    const char *hints = "[A] Select    [B] Back    [D-Pad] Navigate";
    float hints_w = render_text_width(hints, hint_scale);
    render_draw_text(hints, ((float)w - hints_w) * 0.5f,
                     (float)h - hint_scale * 45.0f,
                     hint_scale, 0.5f, 0.5f, 0.6f, 1.0f);
}
