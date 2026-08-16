/**
 * main.c — PlayOS Shell entry point
 *
 * Owns shell state, controller input (direct evdev), lifecycle polling, and
 * the screen update/draw loop. Wayland/EGL surface management is delegated to
 * the Raylib 6.0 PlayOS platform backend
 * (external/raylib/src/platforms/rcore_playos.c).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <stdint.h>

#include "raylib.h"
#include "shell.h"
#include "playos-v1-client-protocol.h"
#include "playos/playos_system.h"
#include "playos/playos_logging.h"
#include "playos/playos_lifecycle.h"
#include "playos/playos_power.h"
#include "playos/playos_audio.h"
#ifdef PLAYOS_TRUSTED_IPC
#include "playos-runtime/trusted_control.h"
#endif

/* ── Global (for signal handler) ─────────────────────────────────────── */
static struct playos_shell *g_shell = NULL;

/* ── UI sounds (Sprint 8) ───────────────────────────────────────────────
 * Procedurally generated short tones; unavailable (and gracefully skipped)
 * when no audio device exists, e.g. under QEMU/CI. */
static Sound g_nav_sound     = { 0 };
static Sound g_confirm_sound = { 0 };
static bool  g_audio_ready   = false;

#define SHELL_TAU 6.283185307179586f

static Sound
shell_make_tone(float freq, float seconds)
{
    const int sample_rate = 44100;
    const int frames = (int)(sample_rate * seconds);

    Wave wave = { 0 };
    wave.frameCount = (unsigned int)frames;
    wave.sampleRate = (unsigned int)sample_rate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = MemAlloc((size_t)frames * sizeof(int16_t));
    if (!wave.data)
        return (Sound){ 0 };

    int16_t *samples = (int16_t *)wave.data;
    for (int i = 0; i < frames; i++) {
        const float t = (float)i / (float)sample_rate;
        samples[i] = (int16_t)(11000.0f * sinf(SHELL_TAU * freq * t));
    }

    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}

static void
shell_play_nav(void)
{
    if (g_audio_ready && IsSoundValid(g_nav_sound))
        PlaySound(g_nav_sound);
}

static void
shell_play_confirm(void)
{
    if (g_audio_ready && IsSoundValid(g_confirm_sound))
        PlaySound(g_confirm_sound);
}

static void
shell_audio_init(void)
{
    InitAudioDevice();
    g_audio_ready = IsAudioDeviceReady();
    if (!g_audio_ready) {
        PLAYOS_LOG_W("shell", "audio device unavailable — UI sounds disabled");
        return;
    }

    g_nav_sound = shell_make_tone(660.0f, 0.07f);
    g_confirm_sound = shell_make_tone(880.0f, 0.12f);

    if (IsSoundValid(g_confirm_sound))
        PlaySound(g_confirm_sound);   /* startup chime */

    PLAYOS_LOG_I("shell", "audio device ready — UI sounds enabled");
}

/* One-time audio bootstrap (Sprint 9 follow-up): the Ally's Realtek codec
 * powers up with the speaker pin muted and volume at minimum, and its card
 * registers a few seconds after boot. The overlay's first-frame bootstrap can
 * be skipped while it is hidden, so the shell (always foreground) retries here
 * every frame until the mixer is available and the defaults have been set.
 * playos_audio_set_*() itself is cheap while the mixer is not yet open
 * (2s cooldown), so calling this per frame is harmless. */
static bool g_audio_defaults_applied = false;

static void
shell_audio_apply_defaults(void)
{
    if (g_audio_defaults_applied)
        return;

    if (playos_audio_set_master_volume(0.7f) == 0 &&
        playos_audio_set_muted(0) == 0) {
        g_audio_defaults_applied = true;
        PLAYOS_LOG_I("shell", "audio defaults applied (volume=0.70, unmuted)");
    }
}

static void
shell_audio_shutdown(void)
{
    if (!g_audio_ready)
        return;

    if (IsSoundValid(g_nav_sound))
        UnloadSound(g_nav_sound);
    if (IsSoundValid(g_confirm_sound))
        UnloadSound(g_confirm_sound);

    CloseAudioDevice();
}

/* ── Analog sticks/triggers (Sprint 12 responsiveness fix) ─────────────
 * Sticks and triggers are decoded directly from evdev in
 * shell_input_poll() (src/input.c), on the same fresh frame as the face
 * buttons. Reading them through Raylib here introduced a one-frame lag,
 * because Raylib refreshes its gamepad snapshot in EndDrawing() — after the
 * shell has already polled this frame. Keeping the whole shell on the
 * trusted evdev path also removes the 5%/10% deadzone split: evdev uses the
 * same fixed 5% deadzone as the platform API (see SHELL_STICK_DEADZONE in
 * src/input.c). */

/* ── Signal handler ──────────────────────────────────────────────────── */

static void
handle_signal(int sig)
{
    (void)sig;
    PLAYOS_LOG_I("shell", "received signal %d, exiting", sig);
    if (g_shell)
        g_shell->running = false;
}

/* ── Power / thermal status bar (Sprint 9) ─────────────────────────────── */

static const char *
shell_perf_profile_name(int profile)
{
    switch (profile) {
    case PLAYOS_PERF_POWER_SAVE:  return "Power Save";
    case PLAYOS_PERF_PERFORMANCE: return "Performance";
    case PLAYOS_PERF_BALANCED:
    default:                      return "Balanced";
    }
}

static const char *
shell_thermal_state_name(int state)
{
    switch (state) {
    case PLAYOS_THERMAL_WARM:     return "Warm";
    case PLAYOS_THERMAL_HOT:      return "Hot";
    case PLAYOS_THERMAL_CRITICAL: return "Critical";
    case PLAYOS_THERMAL_NORMAL:
    default:                      return "Normal";
    }
}

static void
shell_status_refresh(struct playos_shell *s)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (s->power_info_valid) {
        double elapsed = (double)(now.tv_sec - s->last_status_refresh.tv_sec)
                       + (double)(now.tv_nsec - s->last_status_refresh.tv_nsec)
                         / 1e9;
        if (elapsed < 30.0)
            return;
    }

    if (playos_power_get_info(&s->power_info) == 0) {
        s->power_info_valid = true;
        s->last_status_refresh = now;
    }
}

static void
shell_status_bar_draw(struct playos_shell *s)
{
    int w = s->output_width;
    int h = s->output_height;
    float bar_h = (float)h * 0.06f;
    if (bar_h < 24.0f)
        bar_h = 24.0f;
    float scale = bar_h / 24.0f * s->dpi_scale + 0.5f;  /* +0.5 bump for status text */
    float y = (float)h - bar_h;

    render_draw_rect(0.0f, y, (float)w, bar_h, 0.0f, 0.0f, 0.0f, 0.55f);

    char batt[64];
    if (s->power_info.battery_percent >= 0) {
        const char *ac = "";
        if (s->power_info.power_state == PLAYOS_POWER_STATE_CHARGING)
            ac = "  Charging";
        else if (s->power_info.power_state == PLAYOS_POWER_STATE_CHARGED)
            ac = "  AC";
        snprintf(batt, sizeof(batt), "Battery: %d%%%s",
                 s->power_info.battery_percent, ac);
    } else {
        snprintf(batt, sizeof(batt), "Battery: --");
    }

    char temp[96];
    if (s->power_info.cpu_temp_c >= 0 && s->power_info.gpu_temp_c >= 0)
        snprintf(temp, sizeof(temp), "CPU: %dC   GPU: %dC",
                 s->power_info.cpu_temp_c, s->power_info.gpu_temp_c);
    else if (s->power_info.cpu_temp_c >= 0)
        snprintf(temp, sizeof(temp), "CPU: %dC", s->power_info.cpu_temp_c);
    else if (s->power_info.gpu_temp_c >= 0)
        snprintf(temp, sizeof(temp), "GPU: %dC", s->power_info.gpu_temp_c);
    else
        temp[0] = '\0';

    const char *profile = shell_perf_profile_name(s->power_info.active_profile);
    const char *thermal = shell_thermal_state_name(s->power_info.thermal_state);

    float text_y = y + (bar_h - scale * 8.0f) * 0.5f;
    if (text_y < y)
        text_y = y + scale;

    char left[256];
    int n = snprintf(left, sizeof(left), "%s", batt);
    if (temp[0] && n >= 0 && n < (int)sizeof(left))
        n += snprintf(left + n, sizeof(left) - (size_t)n, "    %s", temp);
    if (n >= 0 && n < (int)sizeof(left))
        snprintf(left + n, sizeof(left) - (size_t)n, "    Profile: %s", profile);

    float left_x = (float)w * 0.03f;
    render_draw_text(left, left_x, text_y, scale, 0.85f, 0.85f, 0.9f, 1.0f);

    /* Thermal state on the right, colour-coded. */
    float tr = 0.30f, tg = 0.85f, tb = 0.40f;  /* Normal: green */
    if (s->power_info.thermal_state == PLAYOS_THERMAL_WARM) {
        tr = 1.00f; tg = 0.85f; tb = 0.30f;
    } else if (s->power_info.thermal_state == PLAYOS_THERMAL_HOT) {
        tr = 1.00f; tg = 0.55f; tb = 0.20f;
    } else if (s->power_info.thermal_state == PLAYOS_THERMAL_CRITICAL) {
        tr = 1.00f; tg = 0.25f; tb = 0.25f;
    }

    char thermal_text[64];
    snprintf(thermal_text, sizeof(thermal_text), "Thermal: %s", thermal);
    float thermal_w = render_text_width(thermal_text, scale);
    render_draw_text(thermal_text, (float)w - thermal_w - (float)w * 0.03f,
                     text_y, scale, tr, tg, tb, 1.0f);
}

/* ── Screen navigation helpers ───────────────────────────────────────── */

static void
shell_switch_screen(struct playos_shell *s, enum playos_screen screen)
{
    s->previous_screen = s->current_screen;
    s->current_screen = screen;

    switch (screen) {
    case SCREEN_HOME:        screen_home_enter(s);        break;
    case SCREEN_LIBRARY:     screen_library_enter(s);     break;
    case SCREEN_GAME_DETAIL: screen_game_detail_enter(s); break;
    case SCREEN_SETTINGS:    screen_settings_enter(s);    break;
    }
}

/* Case-insensitive substring check for the DPI detection below. Only the
 * lowercase needles we pass are used, so matching the uppercase counterpart
 * is sufficient. */
static int
shell_str_contains_nocase(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return 0;

    size_t nlen = strlen(needle);
    if (nlen == 0)
        return 1;

    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i]) {
            char c = p[i];
            char n = needle[i];
            if (c != n && c != n - ('a' - 'A'))
                break;
            i++;
        }
        if (i == nlen)
            return 1;
    }

    return 0;
}

/* Detect a coarse UI scale from the device model. The custom Wayland backend
 * cannot report physical monitor size, so we special-case the ROG Ally's
 * 7" 1080p (~314 PPI) panel: without a DPI correction every screen's
 * height-derived text scale is physically tiny. */
static float
shell_detect_dpi_scale(void)
{
    const char *model = playos_system_device_model();
    if (!model || !model[0])
        return 1.0f;

    if (shell_str_contains_nocase(model, "rog") ||
        shell_str_contains_nocase(model, "ally"))
        return 2.0f;

    return 1.0f;
}

/* ── Main ────────────────────────────────────────────────────────────── */

int
main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    struct playos_shell shell_state;
    memset(&shell_state, 0, sizeof(shell_state));
    struct playos_shell *s = &shell_state;
    g_shell = s;

    clock_gettime(CLOCK_MONOTONIC, &s->start_time);
    s->running = true;
    s->current_screen = SCREEN_HOME;
    s->output_width = 1920;
    s->output_height = 1080;
    s->dpi_scale = shell_detect_dpi_scale();
    s->evdev_fd = -1;
    s->reserved_fd_count = 0;

    /* Persistent async-event listener (Sprint 7). Opened by
     * playos_trusted_register_shell() once trusted IPC is up; polled in
     * the main loop for GameStarted/GameExited/GameCrashed events. */
#ifdef PLAYOS_TRUSTED_IPC
    int shell_listener_fd = -1;
#endif

    /* ── Banner ── */
    PLAYOS_LOG_I("shell", "PlayOS Shell v0.1.0 starting");
    PLAYOS_LOG_I("shell", "Raylib %s via rcore_playos.c backend", RAYLIB_VERSION);
    PLAYOS_LOG_I("shell", "Device: %s", playos_system_device_model());
    PLAYOS_LOG_I("shell", "OS: %s", playos_system_os_version());
    PLAYOS_LOG_I("shell", "CPU: %s", playos_system_cpu_description());

    /* ── Input (evdev, trusted) ── */
    if (shell_input_init(s) != 0) {
        PLAYOS_LOG_W("shell", "no gamepad found — continuing without input");
    }

    /* ── Initialize the Raylib window/backend ──
     * The custom backend (rcore_playos.c) connects to Wayland, creates the
     * fullscreen xdg_toplevel + wl_egl_window, and makes an EGL/GLES2
     * context current. */
    InitWindow(s->output_width, s->output_height, "PlayOS Shell");
    if (!IsWindowReady()) {
        PLAYOS_LOG_E("shell", "failed to initialize raylib window/backend");
        return EXIT_FAILURE;
    }

    /* Adopt the compositor-assigned (fullscreen) size. */
    s->output_width = GetScreenWidth();
    s->output_height = GetScreenHeight();
    s->configured = true;
    PLAYOS_LOG_I("shell", "window ready: %dx%d",
                 s->output_width, s->output_height);

    /* Cap the render loop at 60 FPS. raylib's EndDrawing() already performs
     * the frame-time wait via CORE.Time.target, so this gives the shell a
     * stable, consistent cadence instead of spinning unthrottled. */
    SetTargetFPS(60);

    /* ── UI font (Sprint 9) ── */
    render_font_init();

    /* ── UI sounds (Sprint 8) ── */
    shell_audio_init();

    /* ── Register as trusted shell ── */
    struct playos_manager_v1 *mgr = platform_get_playos_manager();
    if (mgr) {
        playos_manager_v1_register_shell(mgr);
        PLAYOS_LOG_I("shell", "registered as trusted shell");

        /* Notify init that the shell is ready (Sprint 5).
         * QueryStatus opens its own connection (connect→send→recv→close);
         * do NOT pre-open a second connection via playos_trusted_connect()
         * — the single-connection IPC server would block reading the unused
         * fd and never serve the real request, deadlocking the shell. */
#ifdef PLAYOS_TRUSTED_IPC
        {
            char status_buf[256];
            if (playos_trusted_query_status(-1, status_buf,
                                            sizeof(status_buf)) == 0) {
                PLAYOS_LOG_I("shell", "shell ready notification sent");
            } else {
                PLAYOS_LOG_W("shell", "shell ready notification failed");
            }
        }

        /* Register a PERSISTENT listener so playos-init can stream
         * GameStarted/GameExited/GameCrashed events to us (S7-T7). */
        shell_listener_fd = playos_trusted_register_shell();
        if (shell_listener_fd >= 0)
            PLAYOS_LOG_I("shell", "async event listener registered (fd %d)",
                         shell_listener_fd);
        else
            PLAYOS_LOG_W("shell", "failed to register async event listener");
#endif
    } else {
        PLAYOS_LOG_W("shell", "playos_manager_v1 not available — "
                     "running without trusted status");
    }

    /* ── Enter home screen ── */
    shell_switch_screen(s, SCREEN_HOME);

    /* ── Signal handlers ── */
    signal(SIGTERM, handle_signal);
    signal(SIGINT,  handle_signal);

    PLAYOS_LOG_I("shell", "entering main loop");

    /* ── Main loop ── */
    int    frame_count = 0;
    struct timespec last_fps_time = s->start_time;

    while (s->running && !WindowShouldClose()) {
        struct timespec frame_start;
        clock_gettime(CLOCK_MONOTONIC, &frame_start);

        /* Input — direct evdev, buttons AND analog axes decoded in
         * shell_input_poll() on one fresh frame (no Raylib one-frame lag). */
        shell_input_poll(s);

        /* One-time audio bootstrap: unmute + default volume once the mixer
         * card becomes available (see shell_audio_apply_defaults()). */
        shell_audio_apply_defaults();

        /* UI sounds: confirm on A, navigation on d-pad / shoulders / back. */
        if (!s->is_suspended) {
            if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH))
                shell_play_confirm();
            else if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP) ||
                     shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN) ||
                     shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_LEFT) ||
                     shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_RIGHT) ||
                     shell_input_button_pressed(s, PLAYOS_BUTTON_L1) ||
                     shell_input_button_pressed(s, PLAYOS_BUTTON_R1) ||
                     shell_input_button_pressed(s, PLAYOS_BUTTON_EAST))
                shell_play_nav();
        }

        /* Lifecycle events from playos-init */
        {
            PlayOSLifecycleEvent ev;
            int ret = playos_lifecycle_poll(&ev);
            if (ret == 1) {
                if (ev == PLAYOS_LIFECYCLE_TERMINATE) {
                    PLAYOS_LOG_I("shell", "lifecycle: terminate received");
                    s->running = false;
                    break;
                } else if (ev == PLAYOS_LIFECYCLE_BACKGROUND ||
                           ev == PLAYOS_LIFECYCLE_SUSPEND) {
                    PLAYOS_LOG_I("shell", "lifecycle: suspend/background (%d)", ev);
                    s->is_suspended = true;
                } else if (ev == PLAYOS_LIFECYCLE_FOREGROUND ||
                           ev == PLAYOS_LIFECYCLE_RESUME) {
                    PLAYOS_LOG_I("shell", "lifecycle: resume/foreground (%d)", ev);
                    s->is_suspended = false;
                }
            }
        }

#ifdef PLAYOS_TRUSTED_IPC
        /* Async game events streamed by playos-init (S7-T7). */
        if (shell_listener_fd >= 0) {
            char ev_type[64] = {0};
            int r = playos_trusted_shell_poll(shell_listener_fd, ev_type,
                                              sizeof(ev_type));
            if (r == 1) {
                if (strcmp(ev_type, PLAYOS_TRUSTED_EVENT_GAME_CRASHED) == 0)
                    PLAYOS_LOG_W("shell", "async: game crashed");
                else if (strcmp(ev_type, PLAYOS_TRUSTED_EVENT_GAME_EXITED) == 0)
                    PLAYOS_LOG_I("shell", "async: game exited");
                else if (strcmp(ev_type, PLAYOS_TRUSTED_EVENT_GAME_STARTED) == 0)
                    PLAYOS_LOG_I("shell", "async: game started");
                else if (strcmp(ev_type, PLAYOS_TRUSTED_EVENT_COMPOSITOR_STATE_CHANGED) == 0)
                    PLAYOS_LOG_I("shell", "async: compositor state changed");
                else if (strcmp(ev_type, PLAYOS_TRUSTED_EVENT_THERMAL_STATE_CHANGED) == 0) {
                    PLAYOS_LOG_I("shell", "async: thermal state changed");
                    s->last_status_refresh.tv_sec = 0;
                    s->last_status_refresh.tv_nsec = 0;
                }
                else if (strcmp(ev_type, PLAYOS_TRUSTED_EVENT_PERF_PROFILE_CHANGED) == 0) {
                    PLAYOS_LOG_I("shell", "async: performance profile changed");
                    s->last_status_refresh.tv_sec = 0;
                    s->last_status_refresh.tv_nsec = 0;
                }
            } else if (r < 0) {
                PLAYOS_LOG_W("shell", "async listener closed by init");
                playos_trusted_disconnect(shell_listener_fd);
                shell_listener_fd = -1;
            }
        }
#endif

        /* Update current screen */
        switch (s->current_screen) {
        case SCREEN_HOME:        screen_home_update(s);        break;
        case SCREEN_LIBRARY:     screen_library_update(s);     break;
        case SCREEN_GAME_DETAIL: screen_game_detail_update(s); break;
        case SCREEN_SETTINGS:    screen_settings_update(s);    break;
        }

        /* Draw current screen — skip while suspended/backgrounded */
        if (!s->is_suspended) {
            shell_status_refresh(s);
            switch (s->current_screen) {
            case SCREEN_HOME:        screen_home_draw(s);        break;
            case SCREEN_LIBRARY:     screen_library_draw(s);     break;
            case SCREEN_GAME_DETAIL: screen_game_detail_draw(s); break;
            case SCREEN_SETTINGS:    screen_settings_draw(s);    break;
            }
            if (s->power_info_valid)
                shell_status_bar_draw(s);
            render_end_frame(s);   /* EndDrawing() + swap via backend */
            frame_count++;
        }

        /* ── Timing ── */
        struct timespec frame_end;
        clock_gettime(CLOCK_MONOTONIC, &frame_end);
        s->frame_time = (frame_end.tv_sec - frame_start.tv_sec) +
                        (frame_end.tv_nsec - frame_start.tv_nsec) / 1e9;
        s->elapsed_time = (frame_end.tv_sec - s->start_time.tv_sec) +
                          (frame_end.tv_nsec - s->start_time.tv_nsec) / 1e9;

        /* FPS counter every 5 seconds */
        double fps_elapsed = (frame_end.tv_sec - last_fps_time.tv_sec) +
                             (frame_end.tv_nsec - last_fps_time.tv_nsec) / 1e9;
        if (fps_elapsed >= 5.0) {
            double fps = frame_count / fps_elapsed;
            PLAYOS_LOG_I("shell", "%.1f fps (%d frames, %.1fms/frame)",
                         fps, frame_count, s->frame_time * 1000.0);
            frame_count = 0;
            last_fps_time = frame_end;
        }
    }

    /* ── Cleanup ── */
    PLAYOS_LOG_I("shell", "shutting down");

    shell_audio_shutdown();

    /* Closes the raylib window: destroys EGL surface/context, the
     * wl_egl_window, xdg surfaces, and disconnects from Wayland. */
    CloseWindow();

    if (s->evdev_fd >= 0)
        close(s->evdev_fd);
    for (int i = 0; i < s->reserved_fd_count; i++) {
        if (s->reserved_fds[i].fd >= 0)
            close(s->reserved_fds[i].fd);
    }

#ifdef PLAYOS_TRUSTED_IPC
    if (shell_listener_fd >= 0)
        playos_trusted_disconnect(shell_listener_fd);
#endif

    return EXIT_SUCCESS;
}
