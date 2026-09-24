/**
 * screen_network.c — Wi-Fi panel for the Settings screen (Sprint 16, T6)
 *
 * Lives inside Settings → Network. Shows the radio's state, the scan list with
 * signal + security, and a passphrase entry via an on-screen keyboard.
 *
 * Every action goes over the trusted control socket:
 *   shell → control.sock → playos-init → playos-net → wpa_supplicant
 * The shell never talks to wpa_supplicant, and never opens the bridge socket.
 *
 * Scanning is the one slow request (seconds, and the control protocol is
 * synchronous), so it runs in a forked child and the reply arrives through a
 * pipe — the UI keeps drawing "Scanning…" instead of freezing for ~3s.
 *
 * SPDX-License-Identifier: MIT
 */
#include "shell.h"
#include "playos/playos_logging.h"
#include "playos/playos_input.h"

#ifdef PLAYOS_TRUSTED_IPC
#include "playos-runtime/trusted_control.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NET_MAX_AP    32
#define NET_SSID_MAX  64
#define NET_PASS_MAX  63
#define NET_JSON_MAX  16384
#define NET_RESCAN_S  20.0      /* idle refresh */
#define NET_STATUS_S  3.0       /* link-state poll */
#define NET_VISIBLE   6         /* rows shown at once (leave room for header) */

typedef struct {
    char ssid[NET_SSID_MAX];
    char security[16];
    int  dbm;
} net_ap;

static struct {
    net_ap ap[NET_MAX_AP];
    int    count;
    int    cursor;
    int    top;                 /* first visible row */

    int    scanning;
    int    have_scanned;
    double next_scan;

    /* keyboard state: letters vs digits/symbols, caps-lock, password visible */
    int    kb_sym;
    int    kb_caps;
    int    kb_show;

    /* link state, polled from NetworkStatus */
    char   state[24];
    char   ssid[NET_SSID_MAX];
    char   ip[64];
    double next_status;

    /* passphrase keyboard */
    int    kb_open;
    char   pass[NET_PASS_MAX + 1];
    int    pass_len;
    int    kb_row;
    int    kb_col;

    char   message[96];

    /* Rows the list can actually show, recomputed every draw from the real
     * viewport height (a fixed count put the cursor off the bottom). */
    int    visible;
} g;

/* ── small helpers ────────────────────────────────────────────────────── */

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Copy "key":"value" out of a flat JSON object. */
static int json_str(const char *json, const char *key, char *out, size_t out_sz)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);

    const char *p = strstr(json, pat);
    if (!p)
        return -1;
    p += strlen(pat);

    size_t j = 0;
    while (*p && *p != '"' && j + 1 < out_sz) {
        if (*p == '\\' && p[1])
            p++;
        out[j++] = *p++;
    }
    out[j] = '\0';
    return 0;
}

static int json_int(const char *json, const char *key, int *out)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);

    const char *p = strstr(json, pat);
    if (!p)
        return -1;
    *out = atoi(p + strlen(pat));
    return 0;
}

/* Parse {"v":1,"type":"ScanResults","networks":[{...},...]}. */
static void parse_networks(const char *json)
{
    int n = 0;
    const char *p = strstr(json, "\"networks\"");
    if (!p)
        return;

    p = strchr(p, '[');
    if (!p)
        return;
    p++;

    while (*p && n < NET_MAX_AP) {
        if (*p == ']')
            break;
        if (*p != '{') {
            p++;
            continue;
        }

        const char *end = strchr(p, '}');
        if (!end)
            break;

        char obj[256];
        size_t len = (size_t)(end - p) + 1;
        if (len >= sizeof(obj))
            len = sizeof(obj) - 1;
        memcpy(obj, p, len);
        obj[len] = '\0';

        char ssid[NET_SSID_MAX] = {0};
        char sec[16] = {0};
        int dbm = 0;

        json_str(obj, "ssid", ssid, sizeof(ssid));
        json_str(obj, "security", sec, sizeof(sec));
        json_int(obj, "signal_dbm", &dbm);

        if (ssid[0]) {
            snprintf(g.ap[n].ssid, sizeof(g.ap[n].ssid), "%s", ssid);
            snprintf(g.ap[n].security, sizeof(g.ap[n].security), "%s",
                     sec[0] ? sec : "open");
            g.ap[n].dbm = dbm;
            n++;
        }

        p = end + 1;
    }

    g.count = n;
    if (g.cursor >= g.count)
        g.cursor = g.count > 0 ? g.count - 1 : 0;
    if (g.top > g.cursor)
        g.top = g.cursor;
}

/* ── requests (control.sock, via init's relay) ────────────────────────── */

static pid_t g_scan_pid = -1;
static int   g_scan_fd = -1;
static char  g_scan_buf[NET_JSON_MAX];
static size_t g_scan_len;

static void scan_start(void)
{
    if (g.scanning || g_scan_pid > 0)
        return;

    g_scan_buf[0] = '\0';
    g_scan_len = 0;

    int pfd[2];
    if (pipe(pfd) != 0)
        return;

    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return;
    }

    if (pid == 0) {
        /* Child: make the request and hand the result to the parent.
         *
         * Deliberately allocation-free (this is a fork of a threaded process —
         * the audio thread can hold the allocator lock across the fork) and
         * deliberately talkative: the first line is the request's return value,
         * so a failure here is diagnosable from the parent's log instead of
         * just surfacing as "scan failed". */
        close(pfd[0]);

        /* The runtime library signals success with 0 and leaves the JSON in the
         * buffer, NUL-terminated — it does NOT return a length. Reading it as a
         * length made every successful scan look like a failure. */
        int rc = -1;
#ifdef PLAYOS_TRUSTED_IPC
        rc = playos_trusted_scan_networks(-1, g_scan_buf, sizeof(g_scan_buf));
#endif
        size_t len = (rc >= 0) ? strlen(g_scan_buf) : 0;

        char hdr[48];
        int hl = snprintf(hdr, sizeof(hdr), "rc=%d len=%zu\n", rc, len);
        if (hl > 0) {
            ssize_t w = write(pfd[1], hdr, (size_t)hl);
            (void)w;
        }
        if (len > 0) {
            ssize_t w = write(pfd[1], g_scan_buf, len);
            (void)w;
        }

        _exit(0);
    }

    close(pfd[1]);
    g_scan_fd = pfd[0];
    fcntl(g_scan_fd, F_SETFL, O_NONBLOCK);
    g_scan_pid = pid;
    g.scanning = 1;
}

/* Non-blocking: collect the child's JSON, then reap it. */
static void scan_poll(void)
{
    if (g_scan_pid <= 0)
        return;

    for (;;) {
        char tmp[1024];
        ssize_t n = read(g_scan_fd, tmp, sizeof(tmp));

        if (n > 0) {
            if (g_scan_len + (size_t)n < sizeof(g_scan_buf)) {
                memcpy(g_scan_buf + g_scan_len, tmp, (size_t)n);
                g_scan_len += (size_t)n;
                g_scan_buf[g_scan_len] = '\0';
            }
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;                     /* still running */
        break;                          /* EOF or error */
    }

    int status = 0;
    waitpid(g_scan_pid, &status, 0);
    close(g_scan_fd);
    g_scan_fd = -1;
    g_scan_pid = -1;
    g.scanning = 0;
    g.next_scan = now_s() + NET_RESCAN_S;

    if (WIFSIGNALED(status))
        PLAYOS_LOG_W("net", "scan child killed by signal %d", WTERMSIG(status));
    else
        PLAYOS_LOG_I("net", "scan child exited with %d", WEXITSTATUS(status));

    /* Child's wire format: "rc=<int> len=<n>\n" then the JSON body. Log both
     * either way, so a failure is diagnosable from the log and not only from a
     * message on screen. */
    int ret = -1;
    size_t blen = 0;
    char *body = strchr(g_scan_buf, '\n');
    int have_hdr = (g_scan_len > 0 &&
                    sscanf(g_scan_buf, "rc=%d len=%zu", &ret, &blen) == 2);

    if (have_hdr)
        PLAYOS_LOG_I("net", "scan child: rc=%d, %zu byte body", ret, blen);
    else
        PLAYOS_LOG_W("net", "scan child: %zu bytes, no header (crashed?)",
                     g_scan_len);

    if (have_hdr && ret == 0 && body && body[1]) {
        parse_networks(body + 1);
        g.have_scanned = 1;
        PLAYOS_LOG_I("net", "scan: %d network(s)", g.count);
        snprintf(g.message, sizeof(g.message), "%d network(s)", g.count);
    } else if (!have_hdr && g_scan_len == 0) {
        snprintf(g.message, sizeof(g.message), "Scan helper died");
    } else {
        snprintf(g.message, sizeof(g.message),
                 "Scan failed (request returned %d)", ret);
    }
}

/* Fast requests: safe to run inline. */
static void status_poll(void)
{
    g.next_status = now_s() + NET_STATUS_S;

#ifdef PLAYOS_TRUSTED_IPC
    static char buf[1024];
    if (playos_trusted_network_status(-1, buf, sizeof(buf)) < 0)
        return;

    char st[24] = {0}, ssid[NET_SSID_MAX] = {0}, ip[64] = {0};
    json_str(buf, "state", st, sizeof(st));
    json_str(buf, "ssid", ssid, sizeof(ssid));
    json_str(buf, "ip", ip, sizeof(ip));

    if (st[0])
        snprintf(g.state, sizeof(g.state), "%s", st);
    snprintf(g.ssid, sizeof(g.ssid), "%s", ssid);
    snprintf(g.ip, sizeof(g.ip), "%s", ip);
#endif
}

static void connect_to(const net_ap *ap, const char *psk)
{
    snprintf(g.message, sizeof(g.message), "Connecting to %.40s...", ap->ssid);

#ifdef PLAYOS_TRUSTED_IPC
    static char buf[1024];
    int rc = playos_trusted_connect_network(-1, ap->ssid, psk, ap->security,
                                            buf, sizeof(buf));
    if (rc < 0) {
        snprintf(g.message, sizeof(g.message), "Connect request failed");
        return;
    }

    if (strstr(buf, "ConnectNetworkError")) {
        char reason[48] = {0};
        json_str(buf, "reason", reason, sizeof(reason));
        snprintf(g.message, sizeof(g.message), "Failed: %s",
                 reason[0] ? reason : "unknown");
        PLAYOS_LOG_W("net", "connect '%.40s' refused: %s", ap->ssid,
                     reason[0] ? reason : "unknown");
        return;
    }

    PLAYOS_LOG_I("shell", "net: connecting to '%.40s' (%s)", ap->ssid,
                 ap->security);
    g.next_status = 0.0;        /* refresh the state line immediately */
#endif
}

static void disconnect_radio(void)
{
#ifdef PLAYOS_TRUSTED_IPC
    static char buf[512];
    playos_trusted_disconnect_network(-1, buf, sizeof(buf));
    snprintf(g.message, sizeof(g.message), "Disconnected");
    g.next_status = 0.0;
#endif
}

/* ── passphrase keyboard ──────────────────────────────────────────────── */

/* Two layouts, switched at runtime with R1: letters, and digits/symbols. Both
 * are three 10-key rows so the cell width is the same in either mode. */
static const char *const KB_LAYOUTS[2][3] = {
    { "qwertyuiop", "asdfghjkl-", "zxcvbnm_. " },
    { "1234567890", "!@#$%^&*()", "-_+=:;,.? " },
};
#define KB_NROWS 3
/* The active layout; g.kb_sym picks the second one. */
#define KB_ROWS (KB_LAYOUTS[g.kb_sym])

static void kb_reset(void)
{
    g.pass_len = 0;
    g.pass[0] = '\0';
    g.kb_row = 0;
    g.kb_col = 0;
    g.kb_caps = 0;
    g.kb_sym = 0;
    g.kb_show = 0;
}

static void kb_type(char c)
{
    /* CAPS-LOCK applies to letters, and only in the letters layout. */
    if (g.kb_caps && !g.kb_sym && c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');

    if (g.pass_len < NET_PASS_MAX) {
        g.pass[g.pass_len++] = c;
        g.pass[g.pass_len] = '\0';
    }
}

static void kb_delete(void)
{
    if (g.pass_len > 0)
        g.pass[--g.pass_len] = '\0';
}

/* ── public API ───────────────────────────────────────────────────────── */

void screen_network_enter(struct playos_shell *s)
{
    (void)s;

    memset(&g, 0, sizeof(g));
    snprintf(g.state, sizeof(g.state), "off");
    scan_start();
}

void screen_network_update(struct playos_shell *s)
{
    scan_poll();

    if (now_s() >= g.next_status)
        status_poll();

    if (!g.scanning && now_s() >= g.next_scan)
        scan_start();

    if (g.kb_open) {
        /* Rows 0..KB_NROWS-1 are the character grid; row KB_NROWS is a control
         * row (CAPS, 123/ABC, SPACE, DEL, SHOW/HIDE). Deliberately NOT on the
         * shoulder buttons: L1/R1 already belong to the settings screen. */
        const int ctrl_row = KB_NROWS;
        int row_len = (g.kb_row == ctrl_row) ? 5
                                             : (int)strlen(KB_ROWS[g.kb_row]);

        if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP)) {
            g.kb_row = (g.kb_row > 0) ? g.kb_row - 1 : ctrl_row;
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN)) {
            g.kb_row = (g.kb_row < ctrl_row) ? g.kb_row + 1 : 0;
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_LEFT)) {
            g.kb_col = (g.kb_col > 0) ? g.kb_col - 1 : row_len - 1;
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_RIGHT)) {
            g.kb_col = (g.kb_col + 1) % row_len;
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
            if (g.kb_row == ctrl_row) {
                switch (g.kb_col) {
                case 0:  g.kb_caps = !g.kb_caps; break;
                case 1:  g.kb_sym = !g.kb_sym;   break;
                case 2:  kb_type(' ');           break;
                case 3:  kb_delete();            break;
                default: g.kb_show = !g.kb_show; break;
                }
            } else {
                kb_type(KB_ROWS[g.kb_row][g.kb_col]);
            }
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_EAST)) {
            kb_delete();
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_WEST)) {
            g.kb_open = 0;
            snprintf(g.message, sizeof(g.message), "Cancelled");
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_NORTH) ||
                   shell_input_button_pressed(s, PLAYOS_BUTTON_START)) {
            if (g.pass_len > 0 && g.count > 0) {
                g.kb_open = 0;
                connect_to(&g.ap[g.cursor], g.pass);
            } else {
                snprintf(g.message, sizeof(g.message),
                         "Enter a passphrase first");
            }
        }

        /* The row may have changed shape; keep the column inside it. Five keys
         * on the control row, so SHOW/HIDE (the last one) stays reachable. */
        int cols = (g.kb_row == ctrl_row) ? 5 : (int)strlen(KB_ROWS[g.kb_row]);
        if (g.kb_col >= cols)
            g.kb_col = cols - 1;
        if (g.kb_col < 0)
            g.kb_col = 0;

        return;
    }

    /* List navigation. */
    if (g.count > 0) {
        if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_UP)) {
            if (g.cursor > 0)
                g.cursor--;
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_DPAD_DOWN)) {
            if (g.cursor + 1 < g.count)
                g.cursor++;
        } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_SOUTH)) {
            const net_ap *ap = &g.ap[g.cursor];
            if (strcmp(ap->security, "open") == 0) {
                connect_to(ap, "");
            } else {
                kb_reset();
                g.kb_open = 1;
                snprintf(g.message, sizeof(g.message),
                         "Passphrase for %.40s", ap->ssid);
            }
        }
    }

    if (shell_input_button_pressed(s, PLAYOS_BUTTON_NORTH)) {
        if (!g.scanning) {
            snprintf(g.message, sizeof(g.message), "Scanning...");
            scan_start();
        }
    } else if (shell_input_button_pressed(s, PLAYOS_BUTTON_WEST)) {
        disconnect_radio();
    }

    /* Keep the cursor inside the window the last draw could actually show. */
    int vis = g.visible > 0 ? g.visible : NET_VISIBLE;
    if (g.cursor < g.top)
        g.top = g.cursor;
    if (g.cursor >= g.top + vis)
        g.top = g.cursor - vis + 1;
}

/* Signal strength → 0..4 bars. */
static int bars_for(int dbm)
{
    if (dbm >= -55) return 4;
    if (dbm >= -65) return 3;
    if (dbm >= -75) return 2;
    if (dbm >= -85) return 1;
    return 0;
}

void screen_network_draw(struct playos_shell *s, float x, float *y,
                         float label_scale, float value_scale)
{
    char line[160];

    /* One text size for the whole panel, taken from the settings screen itself
     * (value_scale is what every other tab uses for its values) so the Wi-Fi tab
     * matches the rest of Settings instead of inventing its own scale — the
     * screens had visibly different type sizes. fontSize = scale*7, so a row of
     * 12*scale leaves the text filling ~58% of it. */
    float header_scale = (float)s->output_height / 240.0f * s->dpi_scale;
    float txt = value_scale;
    float small = value_scale;
    float row_h = txt * 12.0f;

    /* Bottom of the scrolled viewport. Mirrors settings_content_bottom() in
     * screen_settings.c: the panel gets the top of the content area (as *y)
     * but not its bottom, so it has to know where to stop. */
    float content_bottom = (float)s->output_height - header_scale * 22.0f;

    /* ── Link state ────────────────────────────────────────────────── */
    const char *state_txt = g.state[0] ? g.state : "off";
    if (strcmp(state_txt, "connected") == 0 && g.ssid[0])
        snprintf(line, sizeof(line), "Connected - %s", g.ssid);
    else if (strcmp(state_txt, "connecting") == 0)
        snprintf(line, sizeof(line), "Connecting...");
    else
        snprintf(line, sizeof(line), "Not connected");

    render_draw_text("Wi-Fi", x, *y, small, 0.6f, 0.6f, 0.7f, 1.0f);
    render_draw_text(line,
                     (float)s->output_width - x -
                         render_text_width(line, txt),
                     *y, txt, 0.9f, 0.9f, 0.9f, 1.0f);
    *y += row_h;

    if (g.ip[0]) {
        render_draw_text("IP", x, *y, small, 0.6f, 0.6f, 0.7f, 1.0f);
        render_draw_text(g.ip,
                         (float)s->output_width - x -
                             render_text_width(g.ip, txt),
                         *y, txt, 0.9f, 0.9f, 0.9f, 1.0f);
        *y += row_h;
    }
    *y += txt * 2.0f;

    /* ── Passphrase entry ──────────────────────────────────────────── */
    if (g.kb_open) {
        /* Right-aligned like every other value row, so the mask can never be
         * drawn over the label — at a fixed 12*txt offset it landed *inside*
         * "PASSPHRASE" and rendered as "PAS****RASE". A trailing cursor shows
         * where typing goes, and a lone dash means nothing typed yet. */
        char masked[NET_PASS_MAX + 2];
        for (int i = 0; i < g.pass_len; i++)
            masked[i] = g.kb_show ? g.pass[i] : '*';

        if (g.pass_len == 0) {
            masked[0] = '-';
            masked[1] = '\0';
        } else {
            masked[g.pass_len] = '_';
            masked[g.pass_len + 1] = '\0';
        }

        render_draw_text("Passphrase", x, *y, small,
                         0.6f, 0.6f, 0.7f, 1.0f);
        render_draw_text(masked,
                         (float)s->output_width - x -
                             render_text_width(masked, txt),
                         *y, txt, 0.95f, 0.95f, 0.6f, 1.0f);
        *y += row_h * 1.2f;

        /* Spread the keys across the content width. Sizing them from the glyph
         * width left the whole keyboard ~165px wide on a 1920px screen — about
         * 8% of it, and the smallest text on the display. Key height follows
         * the width, capped by the height left below the passphrase line. */
        float cell = (((float)s->output_width - 2.0f * x) * 0.94f) / 10.2f;
        float kb_rh = cell * 1.15f;

        /* Character rows plus one control row. Two rows are reserved below for
         * the button hints, so nothing is drawn past the viewport bottom — the
         * hint line used to be cut off mid-glyph. */
        const int kb_rows_total = KB_NROWS + 1;
        float kb_avail = content_bottom - *y - row_h * 2.0f;
        if (kb_avail > 0.0f && kb_avail / (float)kb_rows_total < kb_rh)
            kb_rh = kb_avail / (float)kb_rows_total;
        if (kb_rh < txt * 3.5f)
            kb_rh = txt * 3.5f;

        /* One text size for the whole panel, as everywhere else in Settings. */
        float kb_scale = txt;

        for (int r = 0; r < KB_NROWS; r++) {
            const char *row = KB_ROWS[r];
            float cx = x;

            for (int c = 0; row[c]; c++) {
                if (r == g.kb_row && c == g.kb_col)
                    render_draw_rect(cx, *y, cell, kb_rh,
                                     0.20f, 0.45f, 0.85f, 0.85f);

                char ch[2] = { row[c], '\0' };
                /* Show what caps-lock will actually type. */
                if (g.kb_caps && !g.kb_sym && ch[0] >= 'a' && ch[0] <= 'z')
                    ch[0] = (char)(ch[0] - 'a' + 'A');

                render_draw_text(ch,
                                 cx + (cell - render_text_width(ch, kb_scale)) * 0.5f,
                                 *y + (kb_rh - kb_scale * 7.0f) * 0.5f,
                                 kb_scale, 0.95f, 0.95f, 0.95f, 1.0f);
                cx += cell * 1.02f;
            }
            *y += kb_rh * 1.12f;
        }

        /* Control row: CAPS, letters<->digits, SPACE, DEL, SHOW/HIDE. On the
         * grid rather than on a shoulder button, which the settings screen
         * already uses. */
        {
            const char *labels[5];
            labels[0] = g.kb_caps ? "CAPS*" : "caps";
            labels[1] = g.kb_sym ? "abc" : "123";
            labels[2] = "SPACE";
            labels[3] = "DEL";
            labels[4] = g.kb_show ? "HIDE" : "SHOW";

            float ccell = (((float)s->output_width - 2.0f * x) * 0.94f) / 5.10f;
            float cx = x;

            for (int c = 0; c < 5; c++) {
                if (g.kb_row == KB_NROWS && c == g.kb_col)
                    render_draw_rect(cx, *y, ccell, kb_rh,
                                     0.20f, 0.45f, 0.85f, 0.85f);
                else if ((c == 0 && g.kb_caps) || (c == 4 && g.kb_show))
                    render_draw_rect(cx, *y, ccell, kb_rh,
                                     0.85f, 0.45f, 0.15f, 0.85f);

                render_draw_text(labels[c],
                                 cx + (ccell - render_text_width(labels[c], kb_scale)) * 0.5f,
                                 *y + (kb_rh - kb_scale * 7.0f) * 0.5f,
                                 kb_scale, 0.95f, 0.95f, 0.95f, 1.0f);
                cx += ccell * 1.02f;
            }
            *y += kb_rh * 1.12f;
        }

        *y += txt * 2.0f;
        render_draw_text("A select   B delete   X cancel   Y connect",
                         x, *y, small, 0.55f, 0.55f, 0.65f, 1.0f);
        *y += row_h;
        return;
    }

    /* ── Scan list ─────────────────────────────────────────────────── */
    if (g.scanning && !g.have_scanned) {
        render_draw_text("Scanning...", x, *y, txt, 0.8f, 0.8f, 0.8f, 1.0f);
        *y += row_h;
    } else if (g.count == 0) {
        render_draw_text(g.have_scanned ? "No networks found"
                                        : "Press Y to scan",
                         x, *y, txt, 0.8f, 0.8f, 0.8f, 1.0f);
        *y += row_h;
    } else {
        /* Fit the list into the space actually left. THREE rows are reserved:
         * the count line, the message line and the button hints — reserving two
         * drew the hint line below the viewport, where it was cut off mid-glyph.
         * Rows shrink (but only to 60% of the pitch) so at least four stay
         * visible. */
        float list_avail = content_bottom - *y - row_h * 3.0f;
        float rh = row_h;
        if (list_avail > 0.0f && list_avail / 4.0f < rh)
            rh = list_avail / 4.0f;
        if (rh < row_h * 0.6f)
            rh = row_h * 0.6f;

        int visible = (list_avail > 0.0f) ? (int)(list_avail / rh) : 1;
        if (visible < 1)
            visible = 1;
        if (visible > NET_MAX_AP)
            visible = NET_MAX_AP;
        g.visible = visible;

        /* One size for the panel's text, same as every other tab. */
        float rscale = txt;

        int last = g.top + visible;
        if (last > g.count)
            last = g.count;

        for (int i = g.top; i < last; i++) {
            const net_ap *ap = &g.ap[i];

            if (i == g.cursor)
                render_draw_rect(x - 6.0f, *y - 3.0f,
                                 (float)s->output_width - 2.0f * x + 12.0f,
                                 rh, 0.20f, 0.45f, 0.85f, 0.55f);

            /* Signal bars FIRST, then the SSID. Drawn to the right of the text
             * they landed on top of long names ("MESSA|RITISNIKHOUSE"). */
            int bars = bars_for(ap->dbm);
            float bar_w = rh * 0.07f;
            float bar_gap = rh * 0.10f;
            for (int b = 0; b < 4; b++) {
                float bh = (float)(b + 1) * rh * 0.13f;
                render_draw_rect(x + (float)b * bar_gap,
                                 *y + (rh * 0.58f - bh),
                                 bar_w, bh,
                                 b < bars ? 0.45f : 0.35f,
                                 b < bars ? 0.95f : 0.35f,
                                 b < bars ? 0.55f : 0.35f,
                                 b < bars ? 1.0f : 0.5f);
            }

            render_draw_text(ap->ssid, x + 5.0f * bar_gap, *y, rscale,
                             1.0f, 1.0f, 1.0f, 1.0f);

            /* Security, right-aligned. The dBm number that used to share this
             * edge overprinted it, and the bars already show the strength. */
            float sw = render_text_width(ap->security, rscale * 0.9f);
            render_draw_text(ap->security,
                             (float)s->output_width - x - sw, *y,
                             rscale * 0.9f, 0.6f, 0.8f, 1.0f, 1.0f);

            *y += rh;
        }

        if (g.count > visible) {
            int last_shown = g.top + visible;
            if (last_shown > g.count)
                last_shown = g.count;
            snprintf(line, sizeof(line), "%s%d-%d of %d%s",
                     g.top > 0 ? "^ " : "", g.top + 1, last_shown, g.count,
                     last_shown < g.count ? " v" : "");
        } else {
            snprintf(line, sizeof(line), "%d networks", g.count);
        }
        *y += txt;
        render_draw_text(line, x, *y, small, 0.55f, 0.55f, 0.65f, 1.0f);
        *y += row_h;
    }

    /* ── Message + controls ────────────────────────────────────────── */
    if (g.message[0]) {
        render_draw_text(g.message, x, *y, small,
                         0.95f, 0.85f, 0.45f, 1.0f);
        *y += row_h;
    }

    render_draw_text("A connect   Y rescan   X disconnect", x, *y,
                     small, 0.55f, 0.55f, 0.65f, 1.0f);
    *y += row_h;
}

int screen_network_keyboard_open(void)
{
    return g.kb_open;
}
