/*
 * frank-war1 — line console on the master's UART0.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_CONSOLE_H
#define FRANK_CONSOLE_H

#include <stdint.h>

typedef struct {
    const char *name;
    void (*fn)(int argc, char **argv);
    const char *help;
} console_cmd_t;

/* Never returns. `idle` runs whenever no input is pending. */
void console_run(const console_cmd_t *cmds, void (*idle)(void));
/* Moves received characters into the console's buffer. Long jobs (the
 * stream's chunk bursts) call it between steps: the UART holds only 32. */
void console_rx_poll(void);
/* The table console_service() runs commands from (console_run sets it). */
void console_set_commands(const console_cmd_t *cmds);
/* Runs the commands received so far, without waiting (from a modal loop). */
void console_service(void);
/* One received byte within timeout_us, or PICO_ERROR_TIMEOUT (raw, for
 * file transfers: a command may read its payload this way). */
int console_getc(uint32_t timeout_us);

#endif
