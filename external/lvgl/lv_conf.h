/**
 * Minimal LVGL configuration for the PlayOS shell spike (Sprint 22, Path 1).
 *
 * Deliberately minimal: LVGL 9 falls back to sensible defaults in
 * lv_conf_internal.h for anything not set here, so this file only states what the
 * spike actually needs. Colour depth matches raylib's RGBA8 texture, and the
 * widgets enabled are the ones the spike's screen and the planned OSK use.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ── Rendering ────────────────────────────────────────────────────────────── */
#define LV_COLOR_DEPTH 32          /* raylib textures are RGBA8 */
#define LV_USE_OS LV_OS_NONE       /* the shell has its own frame loop; no RTOS */

/* ── Logging (routed through LVGL's own log, off by default) ───────────────── */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* ── Widgets the spike uses ───────────────────────────────────────────────── */
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_LIST 1
#define LV_USE_IMAGE 1
#define LV_USE_BAR 1

/* ── Text entry (the OSK this work is for) ────────────────────────────────── */
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1

/* ── Controller navigation ────────────────────────────────────────────────── */
#define LV_USE_GROUP 1
#define LV_USE_GRIDNAV 1

/* ── Fonts ────────────────────────────────────────────────────────────────── */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#endif /* LV_CONF_H */
