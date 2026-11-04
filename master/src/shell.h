/*
 * frank-war1 — the boot shell: welcome and error screens (shell.c).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_SHELL_H
#define FRANK_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True while a shell screen is up (the game's palette is held back). */
extern bool shell_active;

void shell_welcome(void);
/* An error screen: `title` in a red bar, `hint` in the footer. */
void shell_error(const char *title, const char *hint);
/* Keep the welcome up at least min_ms since it was drawn. */
void shell_end(unsigned min_ms);
/* The game's first frame: drop the shell, show `game_rgb` (may be NULL). */
void shell_release(const uint8_t *game_rgb);

/* Lines into the panel's log (and the UART). */
void shell_printf(const char *fmt, ...);
void shell_highlight(const char *fmt, ...);
void shell_errorf(const char *fmt, ...);
/* The game's own boot messages, dim, into the log only. */
void shell_puts_quiet(const char *s);

#ifdef __cplusplus
}
#endif

#endif
