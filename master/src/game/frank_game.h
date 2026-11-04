/*
 * frank-war1 — glue between the game (war1 on port/fsdl.c) and the board.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_GAME_H
#define FRANK_GAME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* war1's main() (war1.c, renamed); returns when the game quits. */
int war_main(int argc, char **argv);

/* Board side, called by the platform layer (plat_frank.c). */
void frank_frame_shown(void);   /* every shown frame */
void frank_service(void);       /* console, statistics */
void frank_quit(void);          /* the game asked to quit: restart */

/* Core 1: the DAC's feed (plat_frank.c). */
bool plat_audio_service(void);
/* Console `mouse`: where the cursor is. */
void plat_set_cursor(int x, int y);

/* The PSRAM heap (sys_fatfs.c). */
uint32_t frank_heap_used(void);
uint32_t frank_heap_unclaimed(void);

#ifdef __cplusplus
}
#endif

#endif
