/*
 * psram_unstick — a RAM-only image for a board in BOOTSEL whose flash reads
 * back as noise (picotool: "The device contents did not match the file"):
 * the PSRAM was left in QPI mode by a reset that did not go through
 * frank_reboot() (the BOOTSEL button, a debugger), and with CS1 floating it
 * answers on the shared QSPI lines. This takes it back to SPI mode, resets
 * it, holds CS1 high and returns to BOOTSEL, no power cycle needed:
 *
 *   cmake -S tools/psram_unstick -B build/psram_unstick -G Ninja
 *   cmake --build build/psram_unstick
 *   picotool load -x build/psram_unstick/unstick.elf
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/structs/qmi.h"
#include "hardware/gpio.h"

static void __no_inline_not_in_flash_func(psram_reset_to_spi)(void) {
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | 0xF5;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    static const uint8_t cmds[2] = {0x66, 0x99};
    for (int i = 0; i < 2; i++) {
        hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
        qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | cmds[i];
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
        hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    }
    for (volatile int i = 0; i < 1000; i++);
    qmi_hw->direct_csr = 0;
}

int main(void) {
    gpio_set_function(47, GPIO_FUNC_XIP_CS1);
    psram_reset_to_spi();
    /* CS1 held high (deselected) from here on */
    gpio_init(47);
    gpio_pull_up(47);
    gpio_put(47, 1);
    gpio_set_dir(47, GPIO_OUT);
    sleep_ms(10);
    reset_usb_boot(0, 0);
}
