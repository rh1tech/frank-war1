#ifndef PSRAM_INIT_H
#define PSRAM_INIT_H

#include "pico/stdlib.h"

// Initialize PSRAM with compile-time frequency
void psram_init(uint cs_pin);

// Initialize PSRAM with runtime-specified frequency (in MHz)
void psram_init_with_freq(uint cs_pin, uint16_t max_freq_mhz);

// Return the PSRAM to its power-on SPI mode (before a software reboot: a
// chip left in QPI mode hangs the boot ROM's flash probe). PSRAM is unusable
// afterwards until psram_init*(). Call with the other core stopped.
void psram_reset_to_spi(void);

#endif
