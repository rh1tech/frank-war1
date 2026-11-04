/*
 * frank-blood — clock bring-up shared by both halves.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Both chips run clk_sys at the same frequency: the link's receive PIO has
 * to finish its loop inside the transmitter's byte period, and each side
 * derives that from its own clk_sys.
 *
 * 504 MHz at 1.65 V: the clock the board's other firmwares (frank-linux,
 * murmsnes) have proven on both halves. 528 MHz was tried for HDMI's sake
 * and produced random single-bit read errors on both chips within a minute.
 *
 *   PLL_SYS  12 MHz x 126 = 1512 MHz VCO / 3 / 1 = 504 MHz -> clk_sys
 *   PLL_USB  48 MHz (SDK default) -> clk_usb
 *   clk_peri = PLL_SYS / 4 = 126 MHz (UART, SPI)
 *   clk_hstx = PLL_SYS / 4 = 126 MHz: 252 Mbit/s TMDS, the 25.2 MHz pixel
 *              clock of 640x480. Nothing else is reachable from 504 MHz.
 *   QMI M0   = flash, /8 = 63 MHz
 *   QMI M1   = PSRAM, /4 = 126 MHz
 */
#include "clocks.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/qmi.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

/* Flash QMI divider for the new clk_sys. Has to run from RAM and before
 * clk_sys rises, or the XIP fetch of the very next instruction runs the
 * flash out of spec.
 *
 * Identical to frank-linux's flash_set_clkdiv, which is proven at 504 MHz on
 * this board. Note that RXDELAY is a 3-bit field: at divisor 8 the computed
 * delay of 8 masks to 0, and that is the configuration that has run for
 * months, so it is kept rather than "corrected" to something untested. */
static void __no_inline_not_in_flash_func(flash_set_clkdiv)(uint32_t clock_hz) {
    const uint32_t max_hz = FRANK_FLASH_MAX_MHZ * 1000000u;
    uint32_t divisor = (clock_hz + max_hz - 1u) / max_hz;
    if (divisor < 1u) divisor = 1u;
    if (divisor > 255u) divisor = 255u;
    uint32_t rxdelay = divisor;
    if (clock_hz / divisor > 100000000u) rxdelay += 1u;
#ifdef FRANK_FLASH_RXDELAY
    rxdelay = FRANK_FLASH_RXDELAY;   /* frank-blood: bench override */
#endif

    uint32_t t = qmi_hw->m[0].timing;
    t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    t |= (divisor << QMI_M0_TIMING_CLKDIV_LSB) |
         ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS);
    qmi_hw->m[0].timing = t;
    __asm__ volatile("dmb" ::: "memory");
    for (volatile int i = 0; i < 1000; i++) __asm__ volatile("nop");
}

bool frank_clocks_init(void) {
    vreg_disable_voltage_limit();
    vreg_set_voltage(FRANK_CPU_VREG);
    sleep_ms(10);

    flash_set_clkdiv(FRANK_CPU_MHZ * 1000000u);
    if (!set_sys_clock_khz(FRANK_CPU_MHZ * 1000u, false))
        return false;

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    sys_hz, sys_hz / 4u);
    return true;
}

bool frank_clocks_set_tmds(uint32_t bit_khz) {
    /* HSTX sends two bits per clk_hstx cycle: clk_hstx = bit rate / 2, and
     * it must divide clk_sys exactly. */
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const uint32_t hstx_hz = bit_khz * 500u;
    if (!hstx_hz || sys_hz % hstx_hz)
        return false;
    return clock_configure(clk_hstx, 0,
                           CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                           sys_hz, hstx_hz);
}

void frank_clocks_report(void) {
    printf("clk_sys  %lu kHz (measured %lu)\n",
           (unsigned long)(clock_get_hz(clk_sys) / 1000u),
           (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS));
    printf("clk_peri %lu kHz, clk_usb %lu kHz (measured %lu)\n",
           (unsigned long)(clock_get_hz(clk_peri) / 1000u),
           (unsigned long)(clock_get_hz(clk_usb) / 1000u),
           (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB));
    const uint32_t m0 = qmi_hw->m[0].timing, m1 = qmi_hw->m[1].timing;
    printf("flash /%lu, psram /%lu, vreg %s\n",
           (unsigned long)((m0 & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB),
           (unsigned long)((m1 & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB),
           FRANK_CPU_VREG == VREG_VOLTAGE_1_65 ? "1.65 V" : "other");
}
