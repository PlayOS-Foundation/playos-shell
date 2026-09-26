/*
 * playos-shell/src/lvgl_spike.h — Sprint 22: LVGL as a widget layer over raylib.
 *
 * Path 1 of the spike: raylib keeps ownership of the window, the GL context and
 * vsync (ADR-0006), while LVGL renders into a texture and is composited by raylib
 * alongside the shell's own drawing. Compiled only when
 * PLAYOS_SHELL_EXPERIMENTAL_LVGL is defined, and enabled at runtime by setting
 * PLAYOS_SHELL_LVGL_SPIKE=1, so an experimental build still runs the normal shell.
 */
#ifndef PLAYOS_SHELL_LVGL_SPIKE_H
#define PLAYOS_SHELL_LVGL_SPIKE_H

/* 1 when the spike is compiled in *and* switched on for this run. */
int  playos_lvgl_spike_enabled(void);

/* Creates the LVGL display and the test screen. Safe to call when disabled. */
void playos_lvgl_spike_init(void);

/* Pumps LVGL (tick + timers) and draws its texture. Call once per frame. */
void playos_lvgl_spike_frame(float dt_seconds);

#endif /* PLAYOS_SHELL_LVGL_SPIKE_H */
