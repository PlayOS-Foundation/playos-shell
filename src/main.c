/**
 * main.c — PlayOS Shell entry point
 *
 * Connects to playos-compositor as a Wayland client, initializes EGL/GLES2,
 * registers as a trusted shell via playos-v1 protocol, and runs the
 * controller-first UI loop.
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
#include <math.h>
#include <sys/time.h>

#include "shell.h"
#include "xdg-shell-client-protocol.h"
#include "playos/playos_system.h"
#include "playos/playos_storage.h"
#include "playos/playos_logging.h"
#include "playos/playos_lifecycle.h"
#ifdef PLAYOS_TRUSTED_IPC
#include "playos-runtime/trusted_control.h"
#endif

/* ── Global (for signal handler) ─────────────────────────────────────── */
static struct playos_shell *g_shell = NULL;

/* ── Forward declarations ────────────────────────────────────────────── */

static int  init_egl(struct playos_shell *s, struct wl_display *wl_display);
static void create_egl_surface(struct playos_shell *s);
static void handle_signal(int sig);

/* ── Registry listener ───────────────────────────────────────────────── */

static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version)
{
    struct playos_shell *s = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        s->compositor = wl_registry_bind(registry, name,
                                         &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        s->xdg_wm_base = wl_registry_bind(registry, name,
                                          &xdg_wm_base_interface, 1);
    } else if (strcmp(interface, playos_manager_v1_interface.name) == 0) {
        s->playos_manager = wl_registry_bind(registry, name,
                                             &playos_manager_v1_interface, 1);
    }
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/* ── EGL init ────────────────────────────────────────────────────────── */

static int
init_egl(struct playos_shell *s, struct wl_display *wl_display)
{
    s->egl_display = eglGetDisplay((EGLNativeDisplayType)wl_display);
    if (s->egl_display == EGL_NO_DISPLAY) {
        PLAYOS_LOG_E("shell", "eglGetDisplay failed");
        return -1;
    }

    EGLint major, minor;
    if (!eglInitialize(s->egl_display, &major, &minor)) {
        PLAYOS_LOG_E("shell", "eglInitialize failed: 0x%x", eglGetError());
        return -1;
    }

    PLAYOS_LOG_I("shell", "EGL %d.%d initialized", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        PLAYOS_LOG_E("shell", "eglBindAPI(ES) failed: 0x%x", eglGetError());
        return -1;
    }

    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(s->egl_display, config_attrs, &config, 1, &num_configs) ||
        num_configs == 0) {
        PLAYOS_LOG_E("shell", "no suitable EGL config");
        return -1;
    }
    s->egl_config = config;

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    s->egl_context = eglCreateContext(s->egl_display, config,
                                      EGL_NO_CONTEXT, ctx_attrs);
    if (s->egl_context == EGL_NO_CONTEXT) {
        PLAYOS_LOG_E("shell", "eglCreateContext failed: 0x%x", eglGetError());
        return -1;
    }

    /* Never throttle — shell must render without blocking */
    eglSwapInterval(s->egl_display, 0);

    return 0;
}

static void
create_egl_surface(struct playos_shell *s)
{
    s->egl_window = wl_egl_window_create(s->surface,
                                         s->output_width, s->output_height);
    if (!s->egl_window) {
        PLAYOS_LOG_E("shell", "wl_egl_window_create failed");
        return;
    }

    s->egl_surface = eglCreateWindowSurface(s->egl_display, s->egl_config,
                                            (EGLNativeWindowType)s->egl_window,
                                            NULL);
    if (s->egl_surface == EGL_NO_SURFACE) {
        PLAYOS_LOG_E("shell", "eglCreateWindowSurface failed: 0x%x",
                     eglGetError());
        return;
    }

    if (!eglMakeCurrent(s->egl_display, s->egl_surface, s->egl_surface,
                        s->egl_context)) {
        PLAYOS_LOG_E("shell", "eglMakeCurrent failed: 0x%x", eglGetError());
        return;
    }

    /* Initialize renderer once we have a GL context */
    render_init(s->output_width, s->output_height);

    /* Log GPU info */
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *vendor   = glGetString(GL_VENDOR);
    const GLubyte *version  = glGetString(GL_VERSION);
    PLAYOS_LOG_I("shell", "GPU: %s / %s / GLES %s",
                 vendor ? (const char *)vendor : "?",
                 renderer ? (const char *)renderer : "?",
                 version ? (const char *)version : "?");
}

/* ── XDG surface listeners ───────────────────────────────────────────── */

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
                             uint32_t serial)
{
    struct playos_shell *s = data;
    xdg_surface_ack_configure(xdg_surface, serial);

    if (!s->egl_surface || s->egl_surface == EGL_NO_SURFACE) {
        create_egl_surface(s);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
                              int32_t w, int32_t h, struct wl_array *states)
{
    struct playos_shell *s = data;
    (void)toplevel; (void)states;

    if (w > 0 && h > 0) {
        s->output_width = w;
        s->output_height = h;
        s->configured = true;

        if (s->egl_window) {
            wl_egl_window_resize(s->egl_window, w, h, 0, 0);
            glViewport(0, 0, w, h);
            render_init(w, h);
        }
    }

    PLAYOS_LOG_I("shell", "toplevel configured: %dx%d", w, h);
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data; (void)toplevel;
    PLAYOS_LOG_I("shell", "close requested");
    if (g_shell)
        g_shell->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

/* ── Signal handler ──────────────────────────────────────────────────── */

static void
handle_signal(int sig)
{
    (void)sig;
    PLAYOS_LOG_I("shell", "received signal %d, exiting", sig);
    if (g_shell)
        g_shell->running = false;
}

/* ── Frame callback (Wayland vsync) ──────────────────────────────────── */

static void
frame_callback_done(void *data, struct wl_callback *cb, uint32_t time)
{
    struct playos_shell *s = data;
    (void)time;
    wl_callback_destroy(cb);
    s->frame_callback = NULL;
    s->frame_pending = false;
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_callback_done,
};

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

int main(int argc, char *argv[])
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

    /* ── Banner ── */
    PLAYOS_LOG_I("shell", "PlayOS Shell v0.1.0 starting");
    PLAYOS_LOG_I("shell", "Device: %s", playos_system_device_model());
    PLAYOS_LOG_I("shell", "OS: %s", playos_system_os_version());
    PLAYOS_LOG_I("shell", "CPU: %s", playos_system_cpu_description());

    /* ── Input (evdev, trusted) ── */
    if (shell_input_init(s) != 0) {
        PLAYOS_LOG_W("shell", "no gamepad found — continuing without input");
    }

    /* ── Wayland connection ── */
    const char *display_name = getenv("WAYLAND_DISPLAY");
    if (!display_name) display_name = "playos-0";

    s->display = wl_display_connect(display_name);
    if (!s->display) {
        PLAYOS_LOG_E("shell", "failed to connect to %s", display_name);
        return EXIT_FAILURE;
    }

    s->registry = wl_display_get_registry(s->display);
    wl_registry_add_listener(s->registry, &registry_listener, s);
    wl_display_roundtrip(s->display);

    if (!s->compositor || !s->xdg_wm_base) {
        PLAYOS_LOG_E("shell", "required Wayland globals missing");
        return EXIT_FAILURE;
    }

    /* ── EGL init ── */
    if (init_egl(s, s->display) != 0)
        return EXIT_FAILURE;

    /* ── Create surface ── */
    s->surface = wl_compositor_create_surface(s->compositor);
    s->xdg_surface = xdg_wm_base_get_xdg_surface(s->xdg_wm_base, s->surface);
    xdg_surface_add_listener(s->xdg_surface, &xdg_surface_listener, s);
    s->xdg_toplevel = xdg_surface_get_toplevel(s->xdg_surface);
    xdg_toplevel_add_listener(s->xdg_toplevel, &xdg_toplevel_listener, s);
    xdg_toplevel_set_title(s->xdg_toplevel, "PlayOS Shell");
    xdg_toplevel_set_fullscreen(s->xdg_toplevel, NULL);
    wl_surface_commit(s->surface);
    wl_display_roundtrip(s->display);

    /* ── Register as trusted shell ── */
    if (s->playos_manager) {
        playos_manager_v1_register_shell(s->playos_manager);
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

    while (s->running) {
        /* ── Frame callback vsync (Sprint 5) ──
         * Request a callback, commit to trigger delivery, then block
         * in wl_display_dispatch until the compositor signals readiness. */
        s->frame_callback = wl_surface_frame(s->surface);
        wl_callback_add_listener(s->frame_callback, &frame_listener, s);
        s->frame_pending = true;
        wl_surface_commit(s->surface);

        while (s->running && s->frame_pending) {
            if (wl_display_dispatch(s->display) < 0)
                break;
        }

        if (!s->running) break;

        struct timespec frame_start;
        clock_gettime(CLOCK_MONOTONIC, &frame_start);

        /* Input */
        if (s->evdev_fd >= 0)
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

        /* Draw current screen */
        if (s->egl_surface != EGL_NO_SURFACE) {
            switch (s->current_screen) {
            case SCREEN_HOME:        screen_home_draw(s);        break;
            case SCREEN_LIBRARY:     screen_library_draw(s);     break;
            case SCREEN_GAME_DETAIL: screen_game_detail_draw(s); break;
            case SCREEN_SETTINGS:    screen_settings_draw(s);    break;
            }

            eglSwapBuffers(s->egl_display, s->egl_surface);
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

    if (s->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (s->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(s->egl_display, s->egl_surface);
        if (s->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(s->egl_display, s->egl_context);
        eglTerminate(s->egl_display);
    }

    if (s->egl_window)    wl_egl_window_destroy(s->egl_window);
    if (s->evdev_fd >= 0) close(s->evdev_fd);
    if (s->playos_shell_iface) playos_shell_v1_destroy(s->playos_shell_iface);
    if (s->playos_manager)     playos_manager_v1_destroy(s->playos_manager);
    if (s->xdg_toplevel)       xdg_toplevel_destroy(s->xdg_toplevel);
    if (s->xdg_surface)        xdg_surface_destroy(s->xdg_surface);
    if (s->surface)            wl_surface_destroy(s->surface);
    if (s->xdg_wm_base)        xdg_wm_base_destroy(s->xdg_wm_base);
    if (s->compositor)         wl_compositor_destroy(s->compositor);
    if (s->registry)           wl_registry_destroy(s->registry);
    if (s->display)            wl_display_disconnect(s->display);

    return EXIT_SUCCESS;
}
