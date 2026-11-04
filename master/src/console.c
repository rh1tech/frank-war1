/*
 * frank-war1 — line console on the master's UART0.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Every command answers with a final line starting "OK" or "ERR" so a host
 * script can wait for completion without knowing the command's output.
 */
#include "console.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/uart.h"

#define LINE_MAX 256
#define ARGS_MAX 12

static void run_line(const console_cmd_t *cmds, char *line) {
    char *argv[ARGS_MAX];
    int argc = 0;
    /* Words, or "quoted strings" (file names with spaces). */
    for (char *p = line; *p && argc < ARGS_MAX;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *end;
        if (*p == '"') {
            argv[argc++] = ++p;
            end = strchr(p, '"');
        } else {
            argv[argc++] = p;
            end = p + strcspn(p, " \t");
        }
        if (!end || !*end) break;
        *end = 0;
        p = end + 1;
    }
    if (!argc)
        return;
    if (!strcmp(argv[0], "help")) {
        for (const console_cmd_t *c = cmds; c->name; c++)
            printf("  %-8s %s\n", c->name, c->help);
        printf("OK\n");
        return;
    }
    for (const console_cmd_t *c = cmds; c->name; c++) {
        if (!strcmp(argv[0], c->name)) {
            c->fn(argc, argv);
            printf("OK %s\n", c->name);
            return;
        }
    }
    printf("ERR unknown command '%s'\n", argv[0]);
}

/* Received characters: the UART's receive interrupt moves them here at
 * once (its FIFO holds 32, under 3 ms at 115200; the main loop can be
 * busier than that, decoding a disk track, and long commands lost
 * characters). */
static volatile char rx_buf[1024];
static volatile uint32_t rx_head, rx_tail;

static void rx_isr(void) {
    while (uart_is_readable(uart0)) {
        const char ch = (char)uart_getc(uart0);
        if (rx_head - rx_tail < sizeof rx_buf)
            rx_buf[rx_head++ % sizeof rx_buf] = ch;
    }
}

void console_rx_poll(void) {
    static bool armed;
    if (armed) return;
    armed = true;
    irq_set_exclusive_handler(UART0_IRQ, rx_isr);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);
}

int console_getc(uint32_t timeout_us) {
    console_rx_poll();
    const uint64_t until = time_us_64() + timeout_us;
    do {
        if (rx_head != rx_tail)
            return (unsigned char)rx_buf[rx_tail++ % sizeof rx_buf];
    } while (time_us_64() < until);
    return PICO_ERROR_TIMEOUT;
}

static const console_cmd_t *the_cmds;
static char line[LINE_MAX];
static size_t line_n;

/* One received character into the line; a complete line runs. */
static void console_char(int ch) {
    if (ch == '\r' || ch == '\n') {
        line[line_n] = 0;
        printf("\n");
        const size_t n = line_n;
        line_n = 0;
        if (n) run_line(the_cmds, line);
        printf("> ");
    } else if ((ch == 8 || ch == 127) && line_n) {
        line_n--;
    } else if (ch >= 32 && line_n < LINE_MAX - 1) {
        line[line_n++] = (char)ch;
    }
}

/* frank-amiga: the characters received so far, without waiting (a modal
 * screen's own loop, ui/menu.c, keeps the console going so). A command
 * that runs from here must not call this again. */
void console_service(void) {
    static bool busy;
    if (!the_cmds || busy) return;
    busy = true;
    for (int ch; (ch = console_getc(0)) != PICO_ERROR_TIMEOUT; )
        console_char(ch);
    busy = false;
}

void console_set_commands(const console_cmd_t *cmds) {
    the_cmds = cmds;
}

void console_run(const console_cmd_t *cmds, void (*idle)(void)) {
    the_cmds = cmds;
    printf("> ");
    for (;;) {
        const int ch = console_getc(1000);
        if (ch == PICO_ERROR_TIMEOUT) {
            if (idle) idle();
            continue;
        }
        console_char(ch);
    }
}
