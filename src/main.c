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

    /* ── Register as trusted shell ── */
    struct playos_manager_v1 *mgr = platform_get_playos_manager();
    if (mgr) {
        playos_manager_v1_register_shell(mgr);
        PLAYOS_LOG_I("shell", "registered as trusted shell");

        /* Notify init that the shell is ready (Sprint 5) */
#ifdef PLAYOS_TRUSTED_IPC
        {
            int cfd = playos_trusted_connect();
            if (cfd >= 0) {
                /* Send a simple ready notification via QueryStatus —
                 * the connection itself signals shell readiness to init */
                char status_buf[256];
                playos_trusted_query_status(cfd, status_buf, sizeof(status_buf));
                playos_trusted_disconnect(cfd);
                PLAYOS_LOG_I("shell", "shell ready notification sent");
            }
        }
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

    /* Closes the raylib window: destroys EGL surface/context, the
     * wl_egl_window, xdg surfaces, and disconnects from Wayland. */
    CloseWindow();

    if (s->evdev_fd >= 0)
        close(s->evdev_fd);

    return EXIT_SUCCESS;
}
