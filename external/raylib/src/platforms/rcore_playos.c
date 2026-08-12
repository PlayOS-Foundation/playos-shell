/**********************************************************************************************
*
*   rcore_playos - PlayOS Shell platform backend (Wayland + EGL/GLES2)
*
*   PLATFORM: PLATFORM_PLAYOS
*       - Native Wayland client backend for the PlayOS shell
*       - Owns: fullscreen xdg_toplevel, wl_egl_window, EGL/GLES2 context,
*         wl_surface_frame pacing and eglSwapBuffers
*
*   NOTES:
*       - Input is intentionally NOT handled here. The PlayOS shell keeps
*         direct evdev input ownership (src/input.c) so SYSTEM/QUICK_MENU
*         reserved buttons survive. PollInputEvents() only resets raylib's
*         internal input state so it never interferes.
*       - playos_manager_v1 is bound during registry discovery and exposed
*         through platform_get_playos_manager() so main.c can perform the
*         trusted shell registration exactly as before.
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2013-2026 Ramon Santamaria (@raysan5) and contributors
*   Copyright (c) 2025 PlayOS Foundation
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include <stdlib.h>         // Required for: getenv()
#include <string.h>         // Required for: strcmp()

#include <wayland-client.h> // Wayland client library
#include <wayland-egl.h>    // Wayland EGL window (wl_egl_window)
#include <EGL/egl.h>        // EGL library
#include <GLES2/gl2.h>      // OpenGL ES 2.0 library

#include "xdg-shell-client-protocol.h"
#include "playos-v1-client-protocol.h"

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
typedef struct {
    struct wl_display       *wl_display;
    struct wl_registry      *registry;
    struct wl_compositor    *compositor;
    struct xdg_wm_base      *xdg_wm_base;
    struct wl_surface       *wl_surface;
    struct xdg_surface      *xdg_surface;
    struct xdg_toplevel     *xdg_toplevel;
    struct wl_egl_window    *egl_window;
    struct playos_manager_v1 *playos_manager;

    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    EGLConfig  egl_config;

    struct wl_callback *frame_callback;
    bool                frame_pending;

    bool configured;
    int  pending_width;
    int  pending_height;
} PlatformData;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
extern CoreData CORE;                   // Global CORE state context

static PlatformData platform = { 0 };   // Platform specific data

//----------------------------------------------------------------------------------
// Module Internal Functions Declaration
//----------------------------------------------------------------------------------
int InitPlatform(void);          // Initialize platform (graphics, inputs and more)
void ClosePlatform(void);        // Close platform

static void platform_set_render_size(int width, int height);   // Update raylib render/viewport state on (re)configure

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
// NOTE: Functions declaration is provided by raylib.h

//----------------------------------------------------------------------------------
// Static helpers
//----------------------------------------------------------------------------------

// Registry listener: bind Wayland globals used by the shell.
static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version)
{
    (void)data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        platform.compositor = wl_registry_bind(registry, name,
                                               &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        platform.xdg_wm_base = wl_registry_bind(registry, name,
                                                &xdg_wm_base_interface, 1);
    } else if (strcmp(interface, playos_manager_v1_interface.name) == 0) {
        platform.playos_manager = wl_registry_bind(registry, name,
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

// XDG surface configure: acknowledge; the real size arrives via the toplevel.
static void
xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
                             uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

// XDG toplevel configure: record the compositor-assigned size and, if the
// EGL surface already exists, resize the native window and raylib viewport.
static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
                              int32_t w, int32_t h, struct wl_array *states)
{
    (void)data; (void)toplevel; (void)states;

    if (w > 0 && h > 0) {
        platform.pending_width = w;
        platform.pending_height = h;
        platform.configured = true;

        if (platform.egl_window) {
            wl_egl_window_resize(platform.egl_window, w, h, 0, 0);
            platform_set_render_size(w, h);
        }

        TRACELOG(LOG_INFO, "PLATFORM: PLAYOS: toplevel configured: %dx%d", w, h);
    }
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data; (void)toplevel;
    TRACELOG(LOG_INFO, "PLATFORM: PLAYOS: close requested");
    CORE.Window.shouldClose = true;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

// Frame callback (Wayland vsync) listener.
static void
frame_callback_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)data; (void)time;
    wl_callback_destroy(cb);
    platform.frame_callback = NULL;
    platform.frame_pending = false;
}

static const struct wl_callback_listener frame_callback_listener = {
    .done = frame_callback_done,
};

// Keep raylib's window/viewport state in sync with the real native size.
static void
platform_set_render_size(int width, int height)
{
    CORE.Window.display.width = width;
    CORE.Window.display.height = height;
    CORE.Window.screen.width = width;
    CORE.Window.screen.height = height;
    CORE.Window.render.width = width;
    CORE.Window.render.height = height;
    CORE.Window.currentFbo.width = width;
    CORE.Window.currentFbo.height = height;
    CORE.Window.renderOffset.x = 0;
    CORE.Window.renderOffset.y = 0;
    CORE.Window.screenScale = MatrixIdentity();

    rlViewport(0, 0, width, height);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, width, height, 0, 0.0f, 1.0f);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Window and Graphics Device
//----------------------------------------------------------------------------------

// Check if application should close
bool WindowShouldClose(void)
{
    if (CORE.Window.ready) return CORE.Window.shouldClose;
    else return true;
}

// Toggle fullscreen mode
void ToggleFullscreen(void)
{
    TRACELOG(LOG_WARNING, "ToggleFullscreen() not available on target platform");
}

// Toggle borderless windowed mode
void ToggleBorderlessWindowed(void)
{
    TRACELOG(LOG_WARNING, "ToggleBorderlessWindowed() not available on target platform");
}

// Set window state: maximized, if resizable
void MaximizeWindow(void)
{
    TRACELOG(LOG_WARNING, "MaximizeWindow() not available on target platform");
}

// Set window state: minimized
void MinimizeWindow(void)
{
    TRACELOG(LOG_WARNING, "MinimizeWindow() not available on target platform");
}

// Restore window from being minimized/maximized
void RestoreWindow(void)
{
    TRACELOG(LOG_WARNING, "RestoreWindow() not available on target platform");
}

// Set window configuration state using flags
void SetWindowState(unsigned int flags)
{
    TRACELOG(LOG_WARNING, "SetWindowState() not available on target platform");
}

// Clear window configuration state flags
void ClearWindowState(unsigned int flags)
{
    TRACELOG(LOG_WARNING, "ClearWindowState() not available on target platform");
}

// Set icon for window
void SetWindowIcon(Image image)
{
    TRACELOG(LOG_WARNING, "SetWindowIcon() not available on target platform");
}

// Set icon for window
void SetWindowIcons(Image *images, int count)
{
    TRACELOG(LOG_WARNING, "SetWindowIcons() not available on target platform");
}

// Set title for window
void SetWindowTitle(const char *title)
{
    CORE.Window.title = title;

    if (platform.xdg_toplevel) xdg_toplevel_set_title(platform.xdg_toplevel, title);
}

// Set window position on screen (windowed mode)
void SetWindowPosition(int x, int y)
{
    TRACELOG(LOG_WARNING, "SetWindowPosition() not available on target platform");
}

// Set monitor for the current window
void SetWindowMonitor(int monitor)
{
    TRACELOG(LOG_WARNING, "SetWindowMonitor() not available on target platform");
}

// Set window minimum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMinSize(int width, int height)
{
    CORE.Window.screenMin.width = width;
    CORE.Window.screenMin.height = height;
}

// Set window maximum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMaxSize(int width, int height)
{
    CORE.Window.screenMax.width = width;
    CORE.Window.screenMax.height = height;
}

// Set window dimensions
void SetWindowSize(int width, int height)
{
    TRACELOG(LOG_WARNING, "SetWindowSize() not available on target platform");
}

// Set window opacity, value opacity is between 0.0 and 1.0
void SetWindowOpacity(float opacity)
{
    TRACELOG(LOG_WARNING, "SetWindowOpacity() not available on target platform");
}

// Set window focused
void SetWindowFocused(void)
{
    TRACELOG(LOG_WARNING, "SetWindowFocused() not available on target platform");
}

// Get native window handle
void *GetWindowHandle(void)
{
    return (void *)platform.wl_display;
}

// Get number of monitors
int GetMonitorCount(void)
{
    return 1;
}

// Get current monitor where window is placed
int GetCurrentMonitor(void)
{
    return 0;
}

// Get selected monitor position
Vector2 GetMonitorPosition(int monitor)
{
    (void)monitor;
    return (Vector2){ 0, 0 };
}

// Get selected monitor width (currently used by monitor)
int GetMonitorWidth(int monitor)
{
    (void)monitor;
    return CORE.Window.display.width;
}

// Get selected monitor height (currently used by monitor)
int GetMonitorHeight(int monitor)
{
    (void)monitor;
    return CORE.Window.display.height;
}

// Get selected monitor physical width in millimetres
int GetMonitorPhysicalWidth(int monitor)
{
    (void)monitor;
    return 0;
}

// Get selected monitor physical height in millimetres
int GetMonitorPhysicalHeight(int monitor)
{
    (void)monitor;
    return 0;
}

// Get selected monitor refresh rate
int GetMonitorRefreshRate(int monitor)
{
    (void)monitor;
    return 0;
}

// Get the human-readable, UTF-8 encoded name of the selected monitor
const char *GetMonitorName(int monitor)
{
    (void)monitor;
    return "PlayOS Display";
}

// Get window position XY on monitor
Vector2 GetWindowPosition(void)
{
    return (Vector2){ 0, 0 };
}

// Get window scale DPI factor for current monitor
Vector2 GetWindowScaleDPI(void)
{
    return (Vector2){ 1.0f, 1.0f };
}

// Set clipboard text content
void SetClipboardText(const char *text)
{
    TRACELOG(LOG_WARNING, "SetClipboardText() not available on target platform");
}

// Get clipboard text content
// NOTE: returned string is allocated and freed by GLFW
const char *GetClipboardText(void)
{
    TRACELOG(LOG_WARNING, "GetClipboardText() not available on target platform");
    return NULL;
}

// Get clipboard image
Image GetClipboardImage(void)
{
    Image image = { 0 };

    TRACELOG(LOG_WARNING, "GetClipboardImage() not available on target platform");

    return image;
}

// Show mouse cursor
void ShowCursor(void)
{
    CORE.Input.Mouse.cursorHidden = false;
}

// Hides mouse cursor
void HideCursor(void)
{
    CORE.Input.Mouse.cursorHidden = true;
}

// Enables cursor (unlock cursor)
void EnableCursor(void)
{
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);
    CORE.Input.Mouse.cursorHidden = false;
}

// Disables cursor (lock cursor)
void DisableCursor(void)
{
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);
    CORE.Input.Mouse.cursorHidden = true;
}

// Swap back buffer with front buffer (screen drawing)
void SwapScreenBuffer(void)
{
    // Wayland vsync: arm a frame callback, commit, then block until the
    // compositor signals readiness for the next frame.
    if (platform.wl_surface) {
        platform.frame_callback = wl_surface_frame(platform.wl_surface);
        wl_callback_add_listener(platform.frame_callback, &frame_callback_listener, NULL);
        platform.frame_pending = true;
        wl_surface_commit(platform.wl_surface);

        while (platform.frame_pending && (wl_display_dispatch(platform.wl_display) >= 0)) { }
    }

    eglSwapBuffers(platform.egl_display, platform.egl_surface);
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Misc
//----------------------------------------------------------------------------------

// Get elapsed time measure in seconds since InitTimer()
double GetTime(void)
{
    double time = 0.0;
    struct timespec ts = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long int nanoSeconds = (unsigned long long int)ts.tv_sec*1000000000LLU + (unsigned long long int)ts.tv_nsec;
    time = (double)(nanoSeconds - CORE.Time.base)*1e-9;  // Elapsed time since InitTimer()

    return time;
}

// Open URL with default system browser (if available)
// NOTE: This function is only safe to use if you control the URL given.
// A user could craft a malicious string performing another action.
// Only call this function yourself not with user input or make sure to check the string yourself.
// Ref: https://github.com/raysan5/raylib/issues/686
void OpenURL(const char *url)
{
    // Security check to (partially) avoid malicious code on target platform
    if (strchr(url, '\'') != NULL) TRACELOG(LOG_WARNING, "SYSTEM: Provided URL could be potentially malicious, avoid [\'] character");
    else
    {
        // TODO: Load url using default browser
    }
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Inputs
//----------------------------------------------------------------------------------

// Set internal gamepad mappings
int SetGamepadMappings(const char *mappings)
{
    TRACELOG(LOG_WARNING, "SetGamepadMappings() not implemented on target platform");
    return 0;
}

// Set gamepad vibration
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration)
{
    TRACELOG(LOG_WARNING, "SetGamepadVibration() not implemented on target platform");
}

// Set mouse position XY
void SetMousePosition(int x, int y)
{
    CORE.Input.Mouse.currentPosition = (Vector2){ (float)x, (float)y };
    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
}

// Set mouse cursor
void SetMouseCursor(int cursor)
{
    TRACELOG(LOG_WARNING, "SetMouseCursor() not implemented on target platform");
}

// Get physical key name.
const char *GetKeyName(int key)
{
    TRACELOG(LOG_WARNING, "GetKeyName() not implemented on target platform");
    return "";
}

// Register all input events
// NOTE: PlayOS shell input is owned by src/input.c (direct evdev). This only
//       resets raylib's internal input state so it never conflicts.
void PollInputEvents(void)
{
#if SUPPORT_GESTURES_SYSTEM
    // NOTE: Gestures update must be called every frame to reset gestures correctly
    // because ProcessGestureEvent() is called on an event, not every frame
    UpdateGestures();
#endif

    // Reset keys/chars pressed registered
    CORE.Input.Keyboard.keyPressedQueueCount = 0;
    CORE.Input.Keyboard.charPressedQueueCount = 0;

    // Reset key repeats
    for (int i = 0; i < MAX_KEYBOARD_KEYS; i++) CORE.Input.Keyboard.keyRepeatInFrame[i] = 0;

    // Reset last gamepad button/axis registered state
    CORE.Input.Gamepad.lastButtonPressed = 0; // GAMEPAD_BUTTON_UNKNOWN

    // Register previous touch states
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) CORE.Input.Touch.previousTouchState[i] = CORE.Input.Touch.currentTouchState[i];

    // Register previous keys states
    for (int i = 0; i < 260; i++)
    {
        CORE.Input.Keyboard.previousKeyState[i] = CORE.Input.Keyboard.currentKeyState[i];
        CORE.Input.Keyboard.keyRepeatInFrame[i] = 0;
    }
}

//----------------------------------------------------------------------------------
// PlayOS-specific accessor (used by main.c for trusted shell registration)
//----------------------------------------------------------------------------------

// Get the bound playos_manager_v1 proxy (may be NULL if the compositor does
// not advertise the PlayOS manager global).
struct playos_manager_v1 *platform_get_playos_manager(void)
{
    return platform.playos_manager;
}

//----------------------------------------------------------------------------------
// Module Internal Functions Definition
//----------------------------------------------------------------------------------

// Initialize platform: graphics, inputs and more
int InitPlatform(void)
{
    FLAG_SET(CORE.Window.flags, FLAG_FULLSCREEN_MODE);

    // Determine the Wayland display name
    const char *display_name = getenv("WAYLAND_DISPLAY");
    if ((display_name == NULL) || (display_name[0] == '\0')) display_name = "playos-0";

    platform.wl_display = wl_display_connect(display_name);
    if (platform.wl_display == NULL)
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: Failed to connect to Wayland display %s", display_name);
        return -1;
    }

    platform.registry = wl_display_get_registry(platform.wl_display);
    wl_registry_add_listener(platform.registry, &registry_listener, NULL);
    wl_display_roundtrip(platform.wl_display);

    if ((platform.compositor == NULL) || (platform.xdg_wm_base == NULL))
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: Required Wayland globals missing");
        return -1;
    }

    // Create the fullscreen xdg_toplevel surface
    platform.wl_surface = wl_compositor_create_surface(platform.compositor);
    platform.xdg_surface = xdg_wm_base_get_xdg_surface(platform.xdg_wm_base, platform.wl_surface);
    xdg_surface_add_listener(platform.xdg_surface, &xdg_surface_listener, NULL);
    platform.xdg_toplevel = xdg_surface_get_toplevel(platform.xdg_surface);
    xdg_toplevel_add_listener(platform.xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(platform.xdg_toplevel,
                           (CORE.Window.title != NULL)? CORE.Window.title : "PlayOS Shell");
    xdg_toplevel_set_fullscreen(platform.xdg_toplevel, NULL);
    wl_surface_commit(platform.wl_surface);
    wl_display_roundtrip(platform.wl_display);

    // Use the compositor-assigned size (fullscreen) when available; fall back
    // to the InitWindow() requested size otherwise.
    int width = CORE.Window.screen.width;
    int height = CORE.Window.screen.height;
    if (platform.configured && (platform.pending_width > 0) && (platform.pending_height > 0))
    {
        width = platform.pending_width;
        height = platform.pending_height;
    }

    // Initialize EGL display and context (mirrors the shell's prior EGL path)
    platform.egl_display = eglGetDisplay((EGLNativeDisplayType)platform.wl_display);
    if (platform.egl_display == EGL_NO_DISPLAY)
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: eglGetDisplay failed");
        return -1;
    }

    EGLint major, minor;
    if (!eglInitialize(platform.egl_display, &major, &minor))
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: eglInitialize failed: 0x%x", eglGetError());
        return -1;
    }

    TRACELOG(LOG_INFO, "PLATFORM: PLAYOS: EGL %d.%d initialized", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API))
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: eglBindAPI(ES) failed: 0x%x", eglGetError());
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
    EGLint num_configs;
    if (!eglChooseConfig(platform.egl_display, config_attrs, &platform.egl_config, 1, &num_configs) ||
        (num_configs == 0))
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: no suitable EGL config");
        return -1;
    }

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    platform.egl_context = eglCreateContext(platform.egl_display, platform.egl_config,
                                            EGL_NO_CONTEXT, ctx_attrs);
    if (platform.egl_context == EGL_NO_CONTEXT)
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: eglCreateContext failed: 0x%x", eglGetError());
        return -1;
    }

    // Never throttle here — frame pacing is handled by wl_surface_frame
    eglSwapInterval(platform.egl_display, 0);

    // Create the native window + EGL surface
    platform.egl_window = wl_egl_window_create(platform.wl_surface, width, height);
    if (platform.egl_window == NULL)
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: wl_egl_window_create failed");
        return -1;
    }

    platform.egl_surface = eglCreateWindowSurface(platform.egl_display, platform.egl_config,
                                                  (EGLNativeWindowType)platform.egl_window, NULL);
    if (platform.egl_surface == EGL_NO_SURFACE)
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: eglCreateWindowSurface failed: 0x%x", eglGetError());
        return -1;
    }

    if (!eglMakeCurrent(platform.egl_display, platform.egl_surface, platform.egl_surface,
                        platform.egl_context))
    {
        TRACELOG(LOG_FATAL, "PLATFORM: PLAYOS: eglMakeCurrent failed: 0x%x", eglGetError());
        return -1;
    }

    // Sync raylib window/render/viewport state to the real native size
    platform_set_render_size(width, height);
    CORE.Window.ready = true;

    // Load OpenGL extensions
    rlLoadExtensions(eglGetProcAddress);

    // Log GPU info
    const GLubyte *gpu_renderer = glGetString(GL_RENDERER);
    const GLubyte *gpu_vendor   = glGetString(GL_VENDOR);
    const GLubyte *gpu_version  = glGetString(GL_VERSION);
    TRACELOG(LOG_INFO, "PLATFORM: PLAYOS: GPU: %s / %s / GLES %s",
             gpu_vendor   ? (const char *)gpu_vendor   : "?",
             gpu_renderer ? (const char *)gpu_renderer : "?",
             gpu_version  ? (const char *)gpu_version  : "?");

    TRACELOG(LOG_INFO, "DISPLAY: Device initialized successfully");
    TRACELOG(LOG_INFO, "    > Display size: %i x %i", CORE.Window.display.width, CORE.Window.display.height);
    TRACELOG(LOG_INFO, "    > Screen size:  %i x %i", CORE.Window.screen.width, CORE.Window.screen.height);
    TRACELOG(LOG_INFO, "    > Render size:  %i x %i", CORE.Window.render.width, CORE.Window.render.height);

    // Initialize timing system
    InitTimer();

    // Initialize storage system
    CORE.Storage.basePath = GetWorkingDirectory();

    TRACELOG(LOG_INFO, "PLATFORM: PLAYOS: Initialized successfully");

    return 0;
}

// Close platform
void ClosePlatform(void)
{
    if (platform.frame_callback)
    {
        wl_callback_destroy(platform.frame_callback);
        platform.frame_callback = NULL;
    }

    if (platform.egl_display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(platform.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (platform.egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(platform.egl_display, platform.egl_surface);
        if (platform.egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(platform.egl_display, platform.egl_context);
        eglTerminate(platform.egl_display);
    }

    if (platform.egl_window)    wl_egl_window_destroy(platform.egl_window);
    if (platform.playos_manager) playos_manager_v1_destroy(platform.playos_manager);
    if (platform.xdg_toplevel)  xdg_toplevel_destroy(platform.xdg_toplevel);
    if (platform.xdg_surface)   xdg_surface_destroy(platform.xdg_surface);
    if (platform.wl_surface)    wl_surface_destroy(platform.wl_surface);
    if (platform.xdg_wm_base)   xdg_wm_base_destroy(platform.xdg_wm_base);
    if (platform.compositor)    wl_compositor_destroy(platform.compositor);
    if (platform.registry)      wl_registry_destroy(platform.registry);
    if (platform.wl_display)    wl_display_disconnect(platform.wl_display);
}

// EOF
