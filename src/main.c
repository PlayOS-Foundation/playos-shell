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

/* ── Signal handler ──────────────────────────────────────────────────── */

static void
handle_signal(int sig)
{
    (void)sig;
    PLAYOS_LOG_I("shell", "received signal %d, exiting", sig);
    if (g_shell)
        g_shell->running = false;
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
    s->dpi_scale = 1.0f;
    s->evdev_fd = -1;

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

        /* Input — direct evdev, all buttons decoded in shell_input_poll() */
        shell_input_poll(s);

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
            switch (s->current_screen) {
            case SCREEN_HOME:        screen_home_draw(s);        break;
            case SCREEN_LIBRARY:     screen_library_draw(s);     break;
            case SCREEN_GAME_DETAIL: screen_game_detail_draw(s); break;
            case SCREEN_SETTINGS:    screen_settings_draw(s);    break;
            }
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

#ifdef PLAYOS_TRUSTED_IPC
    if (shell_listener_fd >= 0)
        playos_trusted_disconnect(shell_listener_fd);
#endif

    return EXIT_SUCCESS;
}
