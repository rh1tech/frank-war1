/*
 * frank-war1 — war1's music on an emulated OPL2 (opl_war.c).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef OPL_WAR_H
#define OPL_WAR_H

#include <stdint.h>

/* The OPL's own rate (3.579545 MHz / 72): no rate converter needed. */
#define OPL_WAR_RATE 49716u

/* Core 1. */
void opl_war_init(void);
void opl_war_midi(uint8_t status, uint8_t a, uint8_t b);
void opl_war_reset(void);
/* Add `frames` frames of music to interleaved 16-bit stereo. */
void opl_war_mix(int16_t *pcm, uint32_t frames);

#endif
