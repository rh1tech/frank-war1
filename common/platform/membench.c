/*
 * frank-blood — memory self-test and access-latency benchmark.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The emulator keeps Amiga chip RAM in PSRAM wherever it does not fit in
 * SRAM, so the numbers that decide the memory map are the cost of a random
 * 16-bit access across a 512 KB window, cached and uncached, against the
 * same access to SRAM. Measured with the DWT cycle counter so the result is
 * in CPU cycles per access, independent of the clock.
 */
#include "membench.h"

#include <stdio.h>
#include <string.h>

#include "hardware/structs/xip_ctrl.h"
#include "pico/stdlib.h"

#define PSRAM_CACHED   0x11000000u
#define PSRAM_UNCACHED 0x15000000u

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

static void cyccnt_enable(void) {
    DEMCR |= 1u << 24;          /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;             /* CYCCNTENA */
}

uint32_t membench_psram_test(uint32_t max_bytes) {
    volatile uint32_t *p = (volatile uint32_t *)PSRAM_UNCACHED;
    /* Size by aliasing: write a distinct word at each power of two and see
     * which ones survive. */
    uint32_t size = 0;
    for (uint32_t s = 1u << 20; s <= max_bytes; s <<= 1)
        p[(s - 4u) / 4u] = s ^ 0xA5A5A5A5u;
    for (uint32_t s = 1u << 20; s <= max_bytes; s <<= 1)
        if (p[(s - 4u) / 4u] == (s ^ 0xA5A5A5A5u))
            size = s;
    if (!size)
        return 0;
    /* Pattern over the first 1 MB, through the uncached alias so the cache
     * cannot hide a bad write. */
    const uint32_t words = (1u << 20) / 4u;
    for (uint32_t i = 0; i < words; i++)
        p[i] = i * 0x9E3779B9u ^ 0x55AA55AAu;
    for (uint32_t i = 0; i < words; i++)
        if (p[i] != (i * 0x9E3779B9u ^ 0x55AA55AAu))
            return 0;
    return size;
}

/* xorshift: cheap and register-only, so the loop measures memory. */
static inline uint32_t step(uint32_t x) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

static uint32_t __no_inline_not_in_flash_func(rand_read16)(uintptr_t base,
                                                           uint32_t mask,
                                                           uint32_t n,
                                                           uint32_t *sink) {
    uint32_t x = 2463534242u, acc = 0;
    const uint32_t t0 = DWT_CYCCNT;
    for (uint32_t i = 0; i < n; i++) {
        x = step(x);
        acc += *(volatile uint16_t *)(base + (x & mask & ~1u));
    }
    const uint32_t t1 = DWT_CYCCNT;
    *sink = acc;
    return t1 - t0;
}

static uint32_t __no_inline_not_in_flash_func(rand_write16)(uintptr_t base,
                                                            uint32_t mask,
                                                            uint32_t n) {
    uint32_t x = 2463534242u;
    const uint32_t t0 = DWT_CYCCNT;
    for (uint32_t i = 0; i < n; i++) {
        x = step(x);
        *(volatile uint16_t *)(base + (x & mask & ~1u)) = (uint16_t)x;
    }
    return DWT_CYCCNT - t0;
}

static uint32_t __no_inline_not_in_flash_func(seq_read32)(uintptr_t base,
                                                          uint32_t bytes,
                                                          uint32_t *sink) {
    uint32_t acc = 0;
    const uint32_t t0 = DWT_CYCCNT;
    for (uint32_t i = 0; i < bytes; i += 4)
        acc += *(volatile uint32_t *)(base + i);
    const uint32_t t1 = DWT_CYCCNT;
    *sink = acc;
    return t1 - t0;
}

/* Overhead of the loop itself (xorshift + mask + add), from SRAM-resident
 * data that always hits, subtracted so the figures are per access. */
static uint32_t loop_cost(uint32_t n) {
    static uint16_t tiny[8];
    uint32_t sink;
    return rand_read16((uintptr_t)tiny, 0xE, n, &sink);
}

void membench_run(void *sram_buf, uint32_t sram_bytes) {
    cyccnt_enable();
    const uint32_t n = 100000;
    uint32_t sink = 0;
    const uint32_t base = loop_cost(n);

#define PER(c) ((float)((c) > base ? (c) - base : 0) / (float)n)
    const uint32_t mask512k = (512u * 1024u) - 1u;

    uint32_t sm = 1;
    while ((sm << 1) <= sram_bytes) sm <<= 1;
    printf("membench: cycles per random 16-bit access (loop %.2f subtracted)\n",
           (double)base / n);
    printf("  SRAM %3lu KB   read %6.2f  write %6.2f\n", (unsigned long)(sm / 1024u),
           (double)PER(rand_read16((uintptr_t)sram_buf, sm - 1u, n, &sink)),
           (double)PER(rand_write16((uintptr_t)sram_buf, sm - 1u, n)));
    printf("  PSRAM 512 KB cached    read %6.2f  write %6.2f\n",
           (double)PER(rand_read16(PSRAM_CACHED, mask512k, n, &sink)),
           (double)PER(rand_write16(PSRAM_CACHED, mask512k, n)));
    printf("  PSRAM 512 KB uncached  read %6.2f  write %6.2f\n",
           (double)PER(rand_read16(PSRAM_UNCACHED, mask512k, n, &sink)),
           (double)PER(rand_write16(PSRAM_UNCACHED, mask512k, n)));
    printf("  PSRAM 16 KB cached     read %6.2f  (fits the XIP cache)\n",
           (double)PER(rand_read16(PSRAM_CACHED, 16u * 1024u - 1u, n, &sink)));
    const uint32_t seq_bytes = 1u << 20;
    const uint32_t c = seq_read32(PSRAM_UNCACHED, seq_bytes, &sink);
    printf("  PSRAM sequential uncached read: %.1f MB/s\n",
           (double)seq_bytes / ((double)c / (double)clock_get_hz(clk_sys)) / 1e6);
    (void)sink;
#undef PER
}
