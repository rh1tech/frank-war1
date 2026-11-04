# frank-blood — settings shared by the master and slave builds.
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Both halves must agree on CPU_MHZ: the link's receive PIO has to finish
# its loop inside the transmitter's byte period, and each side derives that
# from its own clk_sys. build.sh passes the same value to both.

set(CPU_MHZ       "504" CACHE STRING "clk_sys of both halves, MHz")
# frank-blood boards: CORE2 (FRANK Core 2 master) or M2 (Murmulator / FRANK
# M2: same pins; its PSRAM runs at 168 MHz with read delay 6, the values
# frank-quake2 tuned on it).
set(FRANK_BOARD "CORE2" CACHE STRING "Board: CORE2 or M2")
if(FRANK_BOARD STREQUAL "M2")
    set(PSRAM_MAX_MHZ_DEFAULT "168")
    set(PSRAM_RXDELAY_DEFAULT "6")
else()
    set(PSRAM_MAX_MHZ_DEFAULT "133")
    set(PSRAM_RXDELAY_DEFAULT "")
endif()
set(PSRAM_MAX_MHZ "${PSRAM_MAX_MHZ_DEFAULT}" CACHE STRING "PSRAM ceiling, MHz")
set(PSRAM_RXDELAY "${PSRAM_RXDELAY_DEFAULT}" CACHE STRING "PSRAM QMI RXDELAY (half sys clocks); empty = psram_init.c rule")
set(FLASH_MAX_MHZ "101" CACHE STRING "Flash ceiling, MHz")   # frank-blood: /5 = 100.8 MHz
# RXDELAY 2-4 read correctly at 100.8 MHz (`flashcrc`) and boot after a soft
# reset (the timing survives it while clk_sys falls back); 3 is the middle.
set(FLASH_RXDELAY "3" CACHE STRING "Flash QMI RXDELAY (half sys clocks); empty = clocks.c rule")

set(FRANK_PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR}/platform)
set(FRANK_PLATFORM_SOURCES
    ${FRANK_PLATFORM_DIR}/clocks.c
    ${FRANK_PLATFORM_DIR}/membench.c
    ${FRANK_PLATFORM_DIR}/psram_init.c
)
set(FRANK_DEFINITIONS
    FRANK_CPU_MHZ=${CPU_MHZ}
    FRANK_PSRAM_MAX_MHZ=${PSRAM_MAX_MHZ}
    FRANK_FLASH_MAX_MHZ=${FLASH_MAX_MHZ}
    $<$<BOOL:${FLASH_RXDELAY}>:FRANK_FLASH_RXDELAY=${FLASH_RXDELAY}>
    PSRAM_MAX_FREQ_MHZ=${PSRAM_MAX_MHZ}
    $<$<BOOL:${PSRAM_RXDELAY}>:FRANK_PSRAM_RXDELAY=${PSRAM_RXDELAY}>
    # The SDK's own PLL setup is replaced by frank_clocks_init().
    PICO_STDIO_UART_SUPPORT_CHARS_AVAILABLE_CALLBACK=0
)
set(FRANK_COPTS -O2 -Wall -Wno-unused-parameter -Wno-unused-function)
