// PlayOS Shell — Theme.
// Named colour fields replacing every hardcoded Color literal.
// Load from TOML at /usr/share/playos/themes/ for customisation;
// Theme::Dark() provides the default console palette.
#pragma once

#include "raylib.h"

struct Theme {
    // ── Backgrounds ──────────────────────────────────────────────────────
    Color background;       // main screen background
    Color surface;          // panels, dialogs, cards
    Color surfaceInput;     // text-input field background
    Color surfaceButton;    // inactive button background
    Color statusBarBg;      // status bar background (semi-transparent)

    // ── Borders & accents ────────────────────────────────────────────────
    Color border;           // panel border, input border
    Color separator;        // section divider lines
    Color accent;           // primary accent / highlight colour
    Color selected;         // list-item / menu-item selection highlight

    // ── Text ─────────────────────────────────────────────────────────────
    Color textPrimary;      // main text, selected items (≈ RAYWHITE)
    Color textSecondary;    // labels, item descriptions
    Color textMuted;        // help lines, hints, subtle text

    // ── Semantic ─────────────────────────────────────────────────────────
    Color success;          // connected, complete, charged
    Color warning;          // connecting, medium battery, caution
    Color danger;           // error, low battery, destructive
    Color info;             // bluetooth active, IP address, neutral info
    Color inactive;         // disconnected, absent, disabled state

    // ── Overlay ──────────────────────────────────────────────────────────
    Color overlayDim;       // full-screen dim behind overlays

    // ── Factory ──────────────────────────────────────────────────────────
    static Theme Dark();
};
