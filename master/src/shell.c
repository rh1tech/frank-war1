/*
 * frank-war1 — the boot shell: a welcome screen (the boot log in a riveted
 * panel, blood dripping down the margins around it) and the error screens
 * (no SD card, missing game data, a fatal game error), drawn into the frame
 * on screen with their own palette until the game shows its first frame.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Adapted from frank-quake2's boot shell. The drips move in the scanout's
 * frame callback, on core 1, so they keep going while core 0 is busy: that
 * code and its data are in SRAM, and it draws only outside the panel, where
 * core 0 never writes.
 */
#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include "pico/stdlib.h"

#include "display.h"
#include "font6x8.h"

#ifndef FRANK_WAR1_VERSION
#define FRANK_WAR1_VERSION "?"
#endif
#ifndef FRANK_BOARD_NAME
#define FRANK_BOARD_NAME "?"
#endif

#define W DISPLAY_W
#define H DISPLAY_H

enum {
    C_BG = 0,
    C_TEXT,      /* bone */
    C_HI,        /* bright red */
    C_ERR,       /* hot red */
    C_DIM,
    C_PANEL,
    C_BEVEL_LT,
    C_BEVEL,
    C_BEVEL_DK,
    C_ERRBAR,
    C_WHITE,
    C_DRIP0,     /* drip tail .. head */
    C_DRIP1,
    C_DRIP2,
    C_DRIP3,
    C_SHADOW,
    C_TITLE,     /* TITLE_ROWS rows of gradient */
};
#define TITLE_ROWS 32

/* layout (320x200) */
#define PX0 20
#define PY0 6
#define PX1 (W - 20)
#define PY1 (H - 6)
#define BEVEL 4
#define TX0 (PX0 + BEVEL + 6)
#define TX1 (PX1 - BEVEL - 6)
#define TITLE_Y (PY0 + BEVEL + 4)
#define SUB_Y (TITLE_Y + TITLE_ROWS + 6)
#define SEP1_Y (SUB_Y + 21)
#define FOOT_Y (PY1 - BEVEL - 4 - 30)
#define SEP2_Y (FOOT_Y - 5)
#define LOG_COLS ((TX1 - TX0) / 6)

static uint8_t *scr;
static int log_y0, log_rows, log_col, log_row;
static uint32_t shown_us;
bool shell_active;

/* ---- Drips (core 1, in the frame callback) ---- */

#define NDRIPS 28
#define DRIP_LEN 7

typedef struct {
    int16_t x, y;       /* head; y in 1/4 pixels */
    uint8_t speed;      /* 1/4 pixels a frame */
    uint8_t wait;       /* frames before it falls again */
} drip_t;

static drip_t drips[NDRIPS];
static uint32_t drip_seed = 0x6d2b79f5u;
static uint8_t *volatile drip_scr;

static uint32_t __not_in_flash_func(drip_rand)(void) {
    drip_seed ^= drip_seed << 13;
    drip_seed ^= drip_seed >> 17;
    drip_seed ^= drip_seed << 5;
    return drip_seed;
}

/* A new drip: in the left or right margin, starting at the top. */
static void __not_in_flash_func(drip_spawn)(drip_t *d) {
    const uint32_t r = drip_rand();
    const int margin = PX0 - 4;
    const int x = 2 + (int)(r % (uint32_t)margin);
    d->x = (int16_t)((r & 0x100) ? x : W - 1 - x);
    d->y = (int16_t)(-(int)((r >> 12) % 40) * 4);
    d->speed = (uint8_t)(2 + (r >> 20) % 6);
    d->wait = (uint8_t)((r >> 24) % 50);
}

static void __not_in_flash_func(drip_px)(uint8_t *f, int x, int y, uint8_t c) {
    if (y >= 0 && y < H) f[y * W + x] = c;
}

static void __not_in_flash_func(drips_frame)(void) {
    uint8_t *f = drip_scr;
    if (!f) return;
    for (int i = 0; i < NDRIPS; i++) {
        drip_t *d = &drips[i];
        if (d->wait) { d->wait--; continue; }
        const int y0 = d->y >> 2;
        d->y = (int16_t)(d->y + d->speed);
        const int y1 = d->y >> 2;
        if (y1 == y0) continue;
        /* erase what leaves the tail, draw the streak again */
        for (int y = y0 - DRIP_LEN; y < y1 - DRIP_LEN; y++) drip_px(f, d->x, y, C_BG);
        for (int k = 0; k < DRIP_LEN; k++) {
            const uint8_t c = k == 0 ? C_DRIP3 : k < 2 ? C_DRIP2 : k < 4 ? C_DRIP1 : C_DRIP0;
            drip_px(f, d->x, y1 - k, c);
        }
        if (y1 - DRIP_LEN >= H) drip_spawn(d);
    }
}

static void drips_stop(void) {
    display_set_vsync_hook(NULL);
    drip_scr = NULL;
}

static void drips_start(uint8_t *f) {
    for (int i = 0; i < NDRIPS; i++) drip_spawn(&drips[i]);
    drip_scr = f;
    display_set_vsync_hook(drips_frame);
}

/* ---- Drawing ---- */

static void fill(int x0, int y0, int x1, int y1, uint8_t c) {
    for (int y = y0; y < y1; y++) memset(scr + y * W + x0, c, (size_t)(x1 - x0));
}

static uint8_t pal_rgb[256 * 3];

static void palette(void) {
    static const uint8_t base[C_TITLE * 3] = {
        0, 0, 0,        /* bg */
        214, 200, 176,  /* text */
        255, 64, 40,    /* highlight */
        255, 40, 24,    /* error */
        140, 118, 104,  /* dim */
        22, 10, 9,      /* panel */
        150, 54, 44,    /* bevel light */
        96, 24, 20,     /* bevel */
        40, 8, 7,       /* bevel dark */
        140, 10, 6,     /* error bar */
        255, 255, 255,  /* white */
        70, 0, 0,       /* drips, tail to head */
        120, 4, 2,
        176, 12, 6,
        236, 40, 24,
        6, 2, 2,        /* title shadow */
    };
    memset(pal_rgb, 0, sizeof(pal_rgb));
    memcpy(pal_rgb, base, sizeof(base));
    /* the title: bright red down to deep maroon */
    for (int i = 0; i < TITLE_ROWS; i++) {
        uint8_t *p = pal_rgb + (C_TITLE + i) * 3;
        p[0] = (uint8_t)(250 - i * 4);
        p[1] = (uint8_t)(i < 6 ? 60 - i * 8 : 10);
        p[2] = (uint8_t)(i < 6 ? 40 - i * 6 : 8);
    }
    display_set_palette(pal_rgb, 0, 256);
}

static void glyph(int x, int y, char ch, uint8_t c) {
    unsigned u = (unsigned char)ch;
    const uint8_t *g = font6x8[(u >= 32 && u < 127) ? u - 32 : '?' - 32];
    for (int r = 0; r < 8; r++)
        for (int k = 0; k < 6; k++)
            if (g[r] & (0x80 >> k)) scr[(y + r) * W + x + k] = c;
}

static void text(int x, int y, const char *s, uint8_t c) {
    for (; *s; s++, x += 6) glyph(x, y, *s, c);
}

static void text_center(int y, const char *s, uint8_t c) {
    text((W - 6 * (int)strlen(s)) / 2, y, s, c);
}

/* The title, the font scaled 4x: a shadow, the gradient by row, and a few
 * drops running off the bottom of the letters. */
static void title(const char *s) {
    const int scale = 4, n = (int)strlen(s);
    const int x0 = (W - n * 6 * scale) / 2;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            const uint8_t *g = font6x8[(unsigned char)s[i] - 32];
            for (int r = 0; r < 8; r++)
                for (int k = 0; k < 6; k++) {
                    if (!(g[r] & (0x80 >> k))) continue;
                    const int x = x0 + (i * 6 + k) * scale + (pass ? 0 : 2);
                    const int y = TITLE_Y + r * scale + (pass ? 0 : 2);
                    for (int dy = 0; dy < scale; dy++)
                        memset(scr + (y + dy) * W + x, pass ? C_TITLE + r * scale + dy : C_SHADOW,
                               (size_t)scale);
                }
            /* drops under the lowest row of the glyph */
            for (int k = 0; k < 6; k++) {
                if (!(g[6] & (0x80 >> k)) || ((i * 7 + k * 3) % 5)) continue;
                const int x = x0 + (i * 6 + k) * scale + 1;
                const int len = 2 + (i * 5 + k * 11) % 6;
                for (int dy = 0; dy < len; dy++) {
                    const int y = TITLE_Y + 7 * scale + scale + dy;
                    scr[y * W + x] = C_TITLE + TITLE_ROWS - 1;
                    scr[y * W + x + 1] = dy < len - 1 ? C_TITLE + TITLE_ROWS - 1 : C_DRIP1;
                }
            }
        }
}

static void separator(int y) {
    fill(TX0, y, TX1, y + 1, C_BEVEL_DK);
    fill(TX0, y + 1, TX1, y + 2, C_BEVEL);
}

static void rivet(int x, int y) {
    fill(x, y, x + 3, y + 3, C_BEVEL_DK);
    scr[y * W + x] = C_BEVEL_LT;
    scr[y * W + x + 1] = C_BEVEL_LT;
    scr[(y + 1) * W + x] = C_BEVEL_LT;
}

static void panel(void) {
    fill(PX0, PY0, PX1, PY1, C_BEVEL);
    fill(PX0, PY0, PX1, PY0 + 1, C_BEVEL_LT);
    fill(PX0, PY0, PX0 + 1, PY1, C_BEVEL_LT);
    fill(PX0, PY1 - 1, PX1, PY1, C_BEVEL_DK);
    fill(PX1 - 1, PY0, PX1, PY1, C_BEVEL_DK);
    fill(PX0 + BEVEL - 1, PY0 + BEVEL - 1, PX1 - BEVEL + 1, PY1 - BEVEL + 1, C_BEVEL_DK);
    fill(PX0 + BEVEL, PY0 + BEVEL, PX1 - BEVEL, PY1 - BEVEL, C_PANEL);
    rivet(PX0 + BEVEL + 2, PY0 + BEVEL + 2);
    rivet(PX1 - BEVEL - 5, PY0 + BEVEL + 2);
    rivet(PX0 + BEVEL + 2, PY1 - BEVEL - 5);
    rivet(PX1 - BEVEL - 5, PY1 - BEVEL - 5);
}

static void footer(const char *hint) {
    char line[64];
    fill(TX0, FOOT_Y, TX1, FOOT_Y + 30, C_PANEL);
    if (hint) {
        text_center(FOOT_Y, hint, C_HI);
    } else {
        const unsigned sys = (unsigned)(clock_get_hz(clk_sys) / 1000000);
        const unsigned psram = sys / ((qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB);
        const unsigned flash = sys / ((qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB);
        snprintf(line, sizeof(line), "%s  CPU %u  PSRAM %u  FLASH %u MHz", FRANK_BOARD_NAME, sys, psram, flash);
        text_center(FOOT_Y, line, C_DIM);
    }
    text_center(FOOT_Y + 11, "Port (c) 2026 Mikhail Matveev", C_TEXT);
    text_center(FOOT_Y + 22, "github.com/rh1tech/frank-war1", C_HI);
}

/* The whole screen; with an error, a red title bar over the log. */
static void screen(const char *error, const char *hint) {
    drips_stop();
    scr = display_front();
    memset(scr, C_BG, W * H);
    palette();
    panel();
    title("WARCRAFT");
    text_center(SUB_Y, "for RP2350  -  frank-war1 " FRANK_WAR1_VERSION, C_TEXT);
    text_center(SUB_Y + 10, "Warcraft (c) 1994 Blizzard Entertainment", C_DIM);
    separator(SEP1_Y);
    separator(SEP2_Y);
    log_y0 = SEP1_Y + 6;
    if (error) {
        fill(TX0, log_y0, TX1, log_y0 + 12, C_ERRBAR);
        fill(TX0, log_y0, TX1, log_y0 + 1, C_ERR);
        text_center(log_y0 + 2, error, C_WHITE);
        log_y0 += 17;
    }
    log_rows = (SEP2_Y - 3 - log_y0) / 9;
    log_col = log_row = 0;
    footer(hint);
    shell_active = true;
}

void shell_welcome(void) {
    screen(NULL, NULL);
    shown_us = time_us_32();
}

void shell_error(const char *title_text, const char *hint) {
    screen(title_text, hint);
}

void shell_end(unsigned min_ms) {
    while (time_us_32() - shown_us < min_ms * 1000u) tight_loop_contents();
}

/* The game's first frame: hand the screen and the palette over. */
void shell_release(const uint8_t *game_rgb) {
    if (!shell_active) return;
    drips_stop();
    scr = NULL;
    shell_active = false;
    if (game_rgb) display_set_palette(game_rgb, 0, 256);
}

/* ---- The log ---- */

static void log_scroll(void) {
    const int top = log_y0 * W;
    memmove(scr + top, scr + top + 9 * W, (size_t)(9 * W * (log_rows - 1)));
    fill(TX0, log_y0 + 9 * (log_rows - 1), TX1, log_y0 + 9 * log_rows, C_PANEL);
    log_row = log_rows - 1;
}

static void log_newline(void) {
    log_col = 0;
    if (++log_row >= log_rows) log_scroll();
}

static void log_putc(char ch, uint8_t c) {
    if (ch == '\n') { log_newline(); return; }
    if (ch == '\r') {
        log_col = 0;
        fill(TX0, log_y0 + log_row * 9, TX1, log_y0 + log_row * 9 + 9, C_PANEL);
        return;
    }
    if (log_col >= LOG_COLS) log_newline();
    const int x = TX0 + log_col * 6, y = log_y0 + log_row * 9;
    fill(x, y, x + 6, y + 9, C_PANEL);
    glyph(x, y, ch, c);
    log_col++;
}

static void log_text(uint8_t c, const char *buf) {
    if (!scr) return;
    for (const char *s = buf; *s;) {     /* word wrap */
        if (*s != ' ' && *s != '\n' && *s != '\r') {
            int n = 0;
            while (s[n] && s[n] != ' ' && s[n] != '\n' && s[n] != '\r') n++;
            if (log_col > 0 && log_col + n > LOG_COLS) log_newline();
            while (n--) log_putc(*s++, c);
        } else {
            if (*s == ' ' && log_col >= LOG_COLS) log_newline();
            else log_putc(*s, c);
            s++;
        }
    }
}

static void log_vprint(uint8_t c, const char *fmt, va_list ap) {
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    fputs(buf, stdout);
    log_text(c, buf);
}

void shell_puts_quiet(const char *s) {
    log_text(C_DIM, s);
}

void shell_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(C_TEXT, fmt, ap);
    va_end(ap);
}

void shell_highlight(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(C_HI, fmt, ap);
    va_end(ap);
}

void shell_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(C_ERR, fmt, ap);
    va_end(ap);
}
