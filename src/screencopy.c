/*
 * playos-shell/src/screencopy.c — full-output screenshot capture (Sprint 14)
 *
 * The shell's own framebuffer only ever contains the shell surface, so
 * LoadImageFromScreen() can never capture a running game. To get a real
 * screenshot the shell asks the compositor for the composited output through
 * the wlroots zwlr_screencopy_manager_v1 protocol, then encodes the returned
 * wl_shm buffer to PNG with Raylib's ExportImage().
 *
 * The shell shares raylib's Wayland connection (GetWindowHandle() returns the
 * wl_display), so no raylib backend change is needed: this file creates its
 * own registry binding for wl_shm / wl_output / the screencopy manager.
 *
 * Returns 0 when screencopy is unavailable (compositor without the manager,
 * nested/headless test, copy failure) so the caller can fall back to a
 * shell-surface grab.
 */

#define _DEFAULT_SOURCE 1
#include "shell.h"
#include "raylib.h"
#include "playos/playos_logging.h"

#include <wayland-client.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* memfd_create is Linux-specific and reached through syscall() so we do not
 * need _GNU_SOURCE here (the target is musl, the host build is glibc). */
#ifndef SYS_memfd_create
#define SYS_memfd_create 319
#endif

/* ── Per-capture state ────────────────────────────────────────────────── */

struct sc_capture {
    struct wl_display *display;

    struct wl_shm_pool *pool;
    struct wl_buffer   *buffer;
    struct zwlr_screencopy_frame_v1 *frame;

    void   *map;
    size_t  map_size;
    int     fd;

    uint32_t width;
    uint32_t height;
    uint32_t stride;

    int y_invert;
    int done;     /* ready or failed received */
    int failed;
};

/* ── Registry: bind the capture globals once ──────────────────────────── */

struct sc_globals {
    struct wl_shm *shm;
    struct wl_output *output;
    struct zwlr_screencopy_manager_v1 *manager;
    struct wl_registry *registry;
};

static struct sc_globals g_globals;
static int g_globals_ready;

/* wl_output events are required to be handled even though the shell does not
 * use any of the output properties. */
static void
out_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
             int32_t phys_w, int32_t phys_h, int32_t subpixel,
             const char *make, const char *model, int32_t transform)
{
    (void)data; (void)output; (void)x; (void)y; (void)phys_w; (void)phys_h;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void
out_mode(void *data, struct wl_output *output, uint32_t flags,
         int32_t width, int32_t height, int32_t refresh)
{
    (void)data; (void)output; (void)flags; (void)width; (void)height;
    (void)refresh;
}

static void
out_done(void *data, struct wl_output *output)
{
    (void)data; (void)output;
}

static void
out_scale(void *data, struct wl_output *output, int32_t factor)
{
    (void)data; (void)output; (void)factor;
}

static void
out_name(void *data, struct wl_output *output, const char *name)
{
    (void)data; (void)output; (void)name;
}

static void
out_description(void *data, struct wl_output *output, const char *description)
{
    (void)data; (void)output; (void)description;
}

static const struct wl_output_listener sc_output_listener = {
    .geometry    = out_geometry,
    .mode        = out_mode,
    .done        = out_done,
    .scale       = out_scale,
    .name        = out_name,
    .description = out_description,
};

static void
sc_registry_global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version)
{
    struct sc_globals *g = data;

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        g->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        if (!g->output) {
            g->output = wl_registry_bind(registry, name,
                                         &wl_output_interface, 1);
            if (g->output)
                wl_output_add_listener(g->output, &sc_output_listener, NULL);
        }
    } else if (strcmp(interface,
                      zwlr_screencopy_manager_v1_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        g->manager = wl_registry_bind(registry, name,
                                      &zwlr_screencopy_manager_v1_interface, v);
    }
}

static void
sc_registry_global_remove(void *data, struct wl_registry *registry,
                          uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener sc_registry_listener = {
    .global        = sc_registry_global,
    .global_remove = sc_registry_global_remove,
};

/* Bind the capture globals on first use. Returns 1 when the manager, output
 * and shm are all available. */
static int
sc_ensure_globals(struct wl_display *display)
{
    if (g_globals_ready)
        return g_globals.manager && g_globals.output && g_globals.shm;

    g_globals.registry = wl_display_get_registry(display);
    if (!g_globals.registry)
        return 0;

    wl_registry_add_listener(g_globals.registry, &sc_registry_listener,
                             &g_globals);
    wl_display_roundtrip(display);   /* receive globals */
    wl_display_roundtrip(display);   /* receive wl_output events */

    g_globals_ready = 1;

    if (!g_globals.manager || !g_globals.output || !g_globals.shm) {
        PLAYOS_LOG_W("screencopy",
                     "unavailable (manager=%d output=%d shm=%d)",
                     g_globals.manager != NULL, g_globals.output != NULL,
                     g_globals.shm != NULL);
        return 0;
    }
    return 1;
}

/* ── Frame events ─────────────────────────────────────────────────────── */

static void
sc_frame_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame,
                uint32_t format, uint32_t width, uint32_t height,
                uint32_t stride)
{
    struct sc_capture *c = data;
    (void)format;

    size_t size = (size_t)stride * height;

    int fd = (int)syscall(SYS_memfd_create, "playos-screenshot", 0);
    if (fd < 0) {
        PLAYOS_LOG_W("screencopy", "memfd_create failed: %s", strerror(errno));
        c->failed = 1;
        c->done = 1;
        return;
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        PLAYOS_LOG_W("screencopy", "ftruncate failed: %s", strerror(errno));
        close(fd);
        c->failed = 1;
        c->done = 1;
        return;
    }

    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        PLAYOS_LOG_W("screencopy", "mmap failed: %s", strerror(errno));
        close(fd);
        c->failed = 1;
        c->done = 1;
        return;
    }

    c->fd = fd;
    c->map = map;
    c->map_size = size;
    c->width = width;
    c->height = height;
    c->stride = stride;

    c->pool = wl_shm_create_pool(g_globals.shm, fd, (int32_t)size);
    if (!c->pool) {
        c->failed = 1;
        c->done = 1;
        return;
    }
    c->buffer = wl_shm_pool_create_buffer(c->pool, 0, (int32_t)width,
                                          (int32_t)height, (int32_t)stride,
                                          format);
    if (!c->buffer) {
        c->failed = 1;
        c->done = 1;
        return;
    }

    zwlr_screencopy_frame_v1_copy(frame, c->buffer);
}

static void
sc_frame_flags(void *data, struct zwlr_screencopy_frame_v1 *frame,
               uint32_t flags)
{
    (void)frame;
    struct sc_capture *c = data;
    c->y_invert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

static void
sc_frame_ready(void *data, struct zwlr_screencopy_frame_v1 *frame,
               uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
    (void)frame; (void)tv_sec_hi; (void)tv_sec_lo; (void)tv_nsec;
    struct sc_capture *c = data;
    c->done = 1;
}

static void
sc_frame_failed(void *data, struct zwlr_screencopy_frame_v1 *frame)
{
    (void)frame;
    struct sc_capture *c = data;
    c->failed = 1;
    c->done = 1;
}

static const struct zwlr_screencopy_frame_v1_listener sc_frame_listener = {
    .buffer = sc_frame_buffer,
    .flags  = sc_frame_flags,
    .ready  = sc_frame_ready,
    .failed = sc_frame_failed,
};

/* ── Cleanup / export ─────────────────────────────────────────────────── */

static void
sc_capture_cleanup(struct sc_capture *c)
{
    if (c->frame) {
        zwlr_screencopy_frame_v1_destroy(c->frame);
        c->frame = NULL;
    }
    if (c->buffer) {
        wl_buffer_destroy(c->buffer);
        c->buffer = NULL;
    }
    if (c->pool) {
        wl_shm_pool_destroy(c->pool);
        c->pool = NULL;
    }
    if (c->map) {
        munmap(c->map, c->map_size);
        c->map = NULL;
    }
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

/* Convert the captured wl_shm buffer (little-endian B,G,R,X) into a Raylib
 * RGBA image and let Raylib write the PNG. */
static int
sc_export(struct sc_capture *c, const char *path)
{
    if (!c->map || c->width == 0 || c->height == 0 || c->stride == 0)
        return 0;

    size_t px = (size_t)c->width * (size_t)c->height;
    unsigned char *rgba = malloc(px * 4);
    if (!rgba)
        return 0;

    for (uint32_t y = 0; y < c->height; y++) {
        uint32_t src_y = c->y_invert ? (c->height - 1 - y) : y;
        const unsigned char *src =
            (const unsigned char *)c->map + (size_t)src_y * c->stride;
        unsigned char *dst = rgba + (size_t)y * c->width * 4;
        for (uint32_t x = 0; x < c->width; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2];   /* R */
            dst[x * 4 + 1] = src[x * 4 + 1];   /* G */
            dst[x * 4 + 2] = src[x * 4 + 0];   /* B */
            dst[x * 4 + 3] = 255;              /* opaque */
        }
    }

    Image img = {
        .data    = rgba,
        .width   = (int)c->width,
        .height  = (int)c->height,
        .mipmaps = 1,
        .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    int ok = ExportImage(img, path);
    free(rgba);
    return ok;
}

/* ── Public entry point ───────────────────────────────────────────────── */

int
shell_capture_output(const char *path)
{
    if (!path)
        return 0;

    struct wl_display *display = (struct wl_display *)GetWindowHandle();
    if (!display)
        return 0;

    if (!sc_ensure_globals(display))
        return 0;

    struct sc_capture c;
    memset(&c, 0, sizeof(c));
    c.display = display;
    c.fd = -1;

    c.frame = zwlr_screencopy_manager_v1_capture_output(g_globals.manager,
                                                        0, g_globals.output);
    if (!c.frame) {
        PLAYOS_LOG_W("screencopy", "capture_output failed");
        return 0;
    }
    zwlr_screencopy_frame_v1_add_listener(c.frame, &sc_frame_listener, &c);

    /* Pump the connection until the compositor reports ready/failed. Bounded
     * so a stalled compositor cannot wedge the shell. */
    int guard = 0;
    while (!c.done && guard++ < 2000) {
        if (wl_display_roundtrip(display) < 0)
            break;
    }

    int ok = 0;
    if (c.done && !c.failed) {
        ok = sc_export(&c, path);
    } else if (!c.done) {
        PLAYOS_LOG_W("screencopy", "timed out waiting for frame");
    }

    sc_capture_cleanup(&c);
    return ok;
}
