/*
 * frank-blood — clock bring-up shared by both halves.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_CLOCKS_H
#define FRANK_CLOCKS_H

#include <stdbool.h>
#include <stdint.h>

#ifndef FRANK_CPU_MHZ
#define FRANK_CPU_MHZ 504
#endif
#ifndef FRANK_CPU_VREG
#define FRANK_CPU_VREG VREG_VOLTAGE_1_65
#endif
#ifndef FRANK_FLASH_MAX_MHZ
#define FRANK_FLASH_MAX_MHZ 66
#endif
#ifndef FRANK_PSRAM_MAX_MHZ
#define FRANK_PSRAM_MAX_MHZ 133
#endif

/* Raise the core voltage, retime flash, set clk_sys and clk_peri. Call
 * first in main(), before stdio, PSRAM or core 1. */
bool frank_clocks_init(void);

/* clk_hstx = TMDS bit rate / 2 from clk_sys; false if it does not divide
 * clk_sys (at 504 MHz only 252000, 640x480, does). */
bool frank_clocks_set_tmds(uint32_t bit_khz);

void frank_clocks_report(void);

#endif
