/*
 * playos-shell/src/lvgl_spike.c — Sprint 22 T2: the flush_cb renderer.
 *
 * What this is for: proving that LVGL v9 can render a resolution-adaptive,
 * controller-navigated UI *inside* the existing shell, as a widget layer on top of
 * raylib (Path 1). raylib keeps the window, GL context and vsync; LVGL draws into a
 * texture that raylib composites. Nothing here touches rcore_playos.c, the game
 * ABI, or ADR-0006, so the whole spike is additive and reversible.
 *
 * Colour format: RGB888, not ARGB8888. LVGL's 32-bit buffer and raylib's RGBA8
 * agree on component order only after a swap, and the sprint's own notes say to
 * verify byte order rather than assume it. 24-bit RGB matches raylib's
 * PIXELFORMAT_UNCOMPRESSED_R8G8B8 byte-for-byte, and the test screen draws a red
 * and a blue block precisely so a wrong assumption is visible at a glance.
 */

#include "lvgl_spike.h"

#if defined(PLAYOS_SHELL_EXPERIMENTAL_LVGL)

#include <lvgl.h>
#include <raylib.h>
#include <stdlib.h>

#include "shell.h"   /* the shell's controller state, for the LVGL indev */

static int g_w, g_h;   /* the shell's output size */

static Texture2D     g_tex;
static lv_display_t *g_disp;
static uint8_t      *g_buf;
static int           g_frames;
static struct playos_shell *g_shell;   /* for the input device */
static lv_indev_t   *g_indev;
static lv_group_t   *g_group;
static int           g_state = -1;   /* -1 unknown, 0 off, 1 on */

int
playos_lvgl_spike_enabled(void)
{
    if (g_state < 0) {
        const char *v = getenv("PLAYOS_SHELL_LVGL_SPIKE");

        g_state = (v && v[0] == '1') ? 1 : 0;
        if (g_state)
            TraceLog(LOG_INFO, "LVGL: spike enabled, Path 1");
    }
    return g_state;
}

/* LVGL hands us the dirty area and a pixel map for it; that becomes a sub-rectangle
 * upload into the raylib texture. This is the seam the whole approach rests on. */
static void
flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    UpdateTextureRec(g_tex,
                     (Rectangle){ (float)area->x1, (float)area->y1, (float)w, (float)h },
                     px_map);
    lv_display_flush_ready(disp);
}

static lv_obj_t *
block(lv_obj_t *parent, int x, int y, uint32_t colour, const char *text)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, 220, 120);
    lv_obj_set_style_bg_color(obj, lv_color_hex(colour), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(obj);

    lv_label_set_text(label, text);
    lv_obj_center(label);

    return obj;
}

static void
build_screen(void)
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x141820), LV_PART_MAIN);

    g_group = lv_group_create();
    lv_group_set_default(g_group);

    block(scr, 40,  40, 0xFF0000, "RED");
    block(scr, 300, 40, 0x0000FF, "BLUE");
    block(scr, 560, 40, 0x00FF00, "GREEN");

    lv_obj_t *title = lv_label_create(scr);

    lv_label_set_text(title,
                      "PlayOS shell - LVGL spike (Sprint 22 T2)\n"
                      "Path 1: raylib owns the frame, LVGL draws into a texture");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E6F0), LV_PART_MAIN);
    lv_obj_set_pos(title, 40, 220);

    /* Percent units and flex are the reason for using LVGL at all: this row
     * reflows with the display instead of hardcoding pixels. */
    lv_obj_t *row = lv_obj_create(scr);

    lv_obj_set_size(row, lv_pct(90), 160);
    lv_obj_set_pos(row, lv_pct(5), lv_pct(60));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x223044), LV_PART_MAIN);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_button_create(row);
        lv_obj_t *lbl = lv_label_create(btn);

        lv_label_set_text_fmt(lbl, "Item %d", i + 1);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x3E6FA8), LV_PART_MAIN);
        lv_group_add_obj(g_group, btn);
    }

    /* The row is the gridnav container: d-pad moves between its items, and
     * rollover keeps focus inside it. This is the controller navigation the sprint
     * asks LVGL to provide. */
    lv_gridnav_add(row, LV_GRIDNAV_CTRL_ROLLOVER);
}

/* LVGL has no gamepad input type: d-pad and face buttons are translated to
 * LV_KEY_* and a keypad indev drives focus through a group + gridnav. One key per
 * poll, so simultaneous presses resolve by the order below - enough for a spike,
 * and the shape any real implementation would keep. */
static void
indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    struct playos_shell *s = g_shell;

    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;

    if (!s)
        return;

    if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP))
        data->key = LV_KEY_UP;
    else if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN))
        data->key = LV_KEY_DOWN;
    else if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_LEFT))
        data->key = LV_KEY_LEFT;
    else if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_RIGHT))
        data->key = LV_KEY_RIGHT;
    else if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH))
        data->key = LV_KEY_ENTER;      /* A */
    else if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST))
        data->key = LV_KEY_ESC;        /* B */
    else
        return;

    data->state = LV_INDEV_STATE_PRESSED;
}

void
playos_lvgl_spike_init(struct playos_shell *shell, int width, int height)
{
    if (!playos_lvgl_spike_enabled())
        return;

    g_shell = shell;

    g_w = width;
    g_h = height;

    lv_init();

    Image img = GenImageColor(g_w, g_h, BLACK);

    g_tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(g_tex, TEXTURE_FILTER_POINT);

    g_buf = (uint8_t *)MemAlloc((size_t)g_w * g_h * 3);

    g_disp = lv_display_create(g_w, g_h);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB888);
    /* Whole-frame rendering first: correctness before the partial-upload budget
     * (Sprint 22 T2/T4 ordering). */
    lv_display_set_buffers(g_disp, g_buf, NULL, g_w * g_h * 3,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(g_disp, flush_cb);

    build_screen();

    /* Focus: a group holding the spike's buttons, navigated by gridnav. */
    g_indev = lv_indev_create();
    lv_indev_set_type(g_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(g_indev, indev_read_cb);
    lv_indev_set_group(g_indev, g_group);


    TraceLog(LOG_INFO, "LVGL: display %dx%d, RGB888, whole-frame render", g_w, g_h);
}

void
playos_lvgl_spike_frame(float dt_seconds)
{
    if (!playos_lvgl_spike_enabled())
        return;

    g_frames++;

    lv_tick_inc((uint32_t)(dt_seconds * 1000.0f));   /* raylib's frame time is the tick */
    lv_timer_handler();

    DrawTexture(g_tex, 0, 0, WHITE);

    /* Drawn by raylib, on top of LVGL's output: one frame, two layers. */
    DrawText(TextFormat("LVGL frames %d  (%.1f fps)", g_frames,
                        dt_seconds > 0.0f ? 1.0f / dt_seconds : 0.0f),
             20, g_h - 34, 20, RAYWHITE);
}

#else  /* !PLAYOS_SHELL_EXPERIMENTAL_LVGL */

/* Built into every shell; inert unless the spike is compiled in. */
int  playos_lvgl_spike_enabled(void) { return 0; }
void playos_lvgl_spike_init(struct playos_shell *shell, int width, int height) { (void)shell; (void)width; (void)height; }
void playos_lvgl_spike_frame(float dt_seconds) { (void)dt_seconds; }

#endif /* PLAYOS_SHELL_EXPERIMENTAL_LVGL */
