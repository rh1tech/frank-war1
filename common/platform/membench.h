/*
 * frank-blood — memory self-test and access-latency benchmark.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_MEMBENCH_H
#define FRANK_MEMBENCH_H

#include <stdint.h>
#include "hardware/clocks.h"

/* Size PSRAM by aliasing and pattern-test its first megabyte. Returns the
 * size in bytes, or 0 on failure. Destroys the contents. */
uint32_t membench_psram_test(uint32_t max_bytes);

/* Print cycles per random 16-bit access for SRAM, PSRAM cached/uncached. */
void membench_run(void *sram_buf, uint32_t sram_bytes);

#endif
