/*
 * frank-war1 — the boot shell: a welcome screen in the look of Warcraft's
 * main menu (the gold title on dark blue marble, the boot log in a stone
 * framed panel) and the error screens (no SD card, missing game data, a
 * fatal game error), until the game shows its first frame.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Nothing is drawn on screen: the shell draws into a frame of its own (the
 * work frame, never shown) and each change goes on screen whole, as a copy
 * into a frame that is not shown, swapped in at the start of an output
 * frame. Nothing moves or blinks.
 */
#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include "pico/stdlib.h"

#include "display.h"
#include "ui/ui.h"

#ifndef FRANK_WAR1_VERSION
#define FRANK_WAR1_VERSION "?"
#endif
#ifndef FRANK_BOARD_NAME
#define FRANK_BOARD_NAME "?"
#endif

#define W UI_W
#define H UI_H

/* layout (320x200) */
#define LOGO_Y 5
#define SUB_Y 50
#define BOX_X0 22
#define BOX_X1 (W - 22)
#define BOX_Y0 61
#define BOX_Y1 157
#define LOG_X0 (BOX_X0 + 7)
#define LOG_X1 (BOX_X1 - 7)
#define LOG_LINE 10
#define HINT_Y 160
#define FOOT_Y 173

static int work = -1;               /* the frame the shell draws into */
static int log_y0, log_rows, log_x, log_row;
static uint32_t shown_us;
static const char *cur_hint;
bool shell_active;

/* The work frame on screen: copied into a frame that is neither shown nor
 * the work frame, swapped in at the next output frame. */
static void flush(void) {
    const int front = display_front_index();
    int show = 0;
    while (show == work || show == front) show++;
    memcpy(display_buf565(show), display_buf565(work), W * H * 2);
    display_show565(show);
}

void shell_refresh(void) {
    if (shell_active) flush();
}

/* ---- The screen ---- */

static void hint_line(const char *hint) {
    ui_marble(0, HINT_Y, W, HINT_Y + 11);
    if (!hint) return;
    /* "F12" (or a key name in front of the colon) in gold */
    const int fh = UI_FONT_MENU;
    int x = W / 2 - ui_text_width(fh, hint) / 2;
    const char *sp = strstr(hint, "F12");
    if (sp) {
        char head[64];
        const size_t n = (size_t)(sp - hint) < sizeof head - 1 ? (size_t)(sp - hint) : sizeof head - 1;
        memcpy(head, hint, n);
        head[n] = 0;
        x += ui_text(fh, x, HINT_Y, head, UI_CREAM, -1);
        x += ui_text(fh, x, HINT_Y, "F12", UI_GOLD, -1);
        ui_text(fh, x, HINT_Y, sp + 3, UI_CREAM, -1);
    } else {
        ui_text(fh, x, HINT_Y, hint, UI_GOLD, -1);
    }
}

static void footer(void) {
    /* the small font has capitals only */
    ui_text_center(UI_FONT_SMALL, W / 2, FOOT_Y, "(C) 1994 BLIZZARD ENTERTAINMENT", UI_GOLD, -1);
    ui_text_center(UI_FONT_SMALL, W / 2, FOOT_Y + 9,
                   "frank-war1 " FRANK_WAR1_VERSION "  -  port (c) 2026 Mikhail Matveev", UI_CREAM, -1);
    ui_text_center(UI_FONT_SMALL, W / 2, FOOT_Y + 18, "github.com/rh1tech/frank-war1", UI_DIM, -1);
}

void shell_clocks(char *line, size_t n) {
    const unsigned sys = (unsigned)(clock_get_hz(clk_sys) / 1000000);
    const unsigned psram = sys / ((qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB);
    const unsigned flash = sys / ((qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB);
    snprintf(line, n, "CPU %u MHz, PSRAM %u MHz, flash %u MHz", sys, psram, flash);
}

/* The whole screen; with an error, a red banner over the log. */
static void screen(const char *error, const char *hint) {
    const int front = display_front_index();
    work = front == 2 ? 1 : 2;
    ui_target(display_buf565(work));
    ui_marble(0, 0, W, H);
    ui_logo(W / 2, LOGO_Y);
    ui_text_spaced(UI_FONT_SMALL, W / 2, SUB_Y, "ORCS & HUMANS", UI_RGB(255, 219, 82), 3);
    ui_box(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, UI_NAVY, false);
    log_y0 = BOX_Y0 + 5;
    if (error) {
        ui_box(BOX_X0 + 4, BOX_Y0 + 4, BOX_X1 - 4, BOX_Y0 + 20, UI_BLOOD, true);
        ui_text_center(UI_FONT_MENU, W / 2, BOX_Y0 + 7, error, UI_CREAM, -1);
        log_y0 = BOX_Y0 + 24;
    }
    log_rows = (BOX_Y1 - 4 - log_y0) / LOG_LINE;
    log_x = LOG_X0;
    log_row = 0;
    cur_hint = hint;
    hint_line(hint);
    footer();
    shell_active = true;
    flush();
}

void shell_welcome(void) {
    screen(NULL, NULL);
    shown_us = time_us_32();
}

void shell_error(const char *title_text, const char *hint) {
    screen(title_text, hint);
}

void shell_hint(const char *hint) {
    if (!shell_active || hint == cur_hint) return;
    cur_hint = hint;
    ui_target(display_buf565(work));
    hint_line(hint);
    flush();
}

void shell_end(unsigned min_ms) {
    while (time_us_32() - shown_us < min_ms * 1000u) tight_loop_contents();
}

uint32_t shell_shown_ms(void) {
    return (time_us_32() - shown_us) / 1000u;
}

int shell_work_frame(void) {
    return shell_active ? work : -1;
}

/* The game's first frame: the screen is the game's from now on. */
void shell_release(const uint8_t *game_rgb) {
    (void)game_rgb;
    if (!shell_active) return;
    shell_active = false;
    work = -1;
    ui_release();
}

/* ---- The log ---- */

static uint16_t log_color;

static void log_scroll(void) {
    uint16_t *f = ui_fb();
    for (int r = 0; r < (log_rows - 1) * LOG_LINE; r++)
        memcpy(f + (log_y0 + r) * W + LOG_X0, f + (log_y0 + r + LOG_LINE) * W + LOG_X0, (LOG_X1 - LOG_X0) * 2);
    ui_fill(LOG_X0, log_y0 + (log_rows - 1) * LOG_LINE, LOG_X1, log_y0 + log_rows * LOG_LINE, UI_NAVY);
    log_row = log_rows - 1;
}

static void log_newline(void) {
    log_x = LOG_X0;
    if (++log_row >= log_rows) log_scroll();
}

static void log_word(const char *s, int n) {
    char buf[96];
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    const int w = ui_text_width(UI_FONT_MENU, buf);
    if (log_x > LOG_X0 && log_x + w > LOG_X1) log_newline();
    log_x += ui_text(UI_FONT_MENU, log_x, log_y0 + log_row * LOG_LINE, buf, log_color, -1) + 1;
}

static void log_text(uint16_t c, const char *buf) {
    if (!shell_active) return;
    ui_target(display_buf565(work));
    log_color = c;
    const int space = ui_text_width(UI_FONT_MENU, " ") + 1;
    for (const char *s = buf; *s;) {     /* word wrap */
        if (*s == '\n') {
            log_newline();
            s++;
        } else if (*s == '\r') {
            log_x = LOG_X0;
            ui_fill(LOG_X0, log_y0 + log_row * LOG_LINE, LOG_X1, log_y0 + (log_row + 1) * LOG_LINE, UI_NAVY);
            s++;
        } else if (*s == ' ') {
            if (log_x > LOG_X0) log_x += space;
            s++;
        } else {
            int n = 0;
            while (s[n] && s[n] != ' ' && s[n] != '\n' && s[n] != '\r') n++;
            log_word(s, n);
            s += n;
        }
    }
    flush();
}

static void log_vprint(uint16_t c, const char *fmt, va_list ap) {
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    fputs(buf, stdout);
    log_text(c, buf);
}

void shell_puts_quiet(const char *s) {
    log_text(UI_DIM, s);
}

void shell_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(UI_CREAM, fmt, ap);
    va_end(ap);
}

void shell_highlight(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(UI_GOLD, fmt, ap);
    va_end(ap);
}

void shell_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprint(UI_RED, fmt, ap);
    va_end(ap);
}
