# frank-war1 — firmware (RP2350B): Warcraft: Orcs & Humans on one chip.
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.13)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PICO_BOARD_HEADER_DIRS ${REPO_ROOT}/common/boards CACHE PATH "" FORCE)
set(PICO_BOARD "frank_core2_master" CACHE STRING "Pico board type" FORCE)

include(${REPO_ROOT}/common/frank.cmake)
include(${REPO_ROOT}/pico_sdk_import.cmake)
project(frank-war1 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(lib/fatfs)
include(lib/sdcard/sdcard.cmake)
include(game.cmake)

add_executable(frank-war1
    src/main.c
    src/console.c
    src/display.c
    src/shell.c
    src/font6x8.c
    src/video/hstx_video.c
    src/video/hdmi_island.c
    src/audio/audio.c
    src/usb/hid_app.c
    src/usb/xinput_host.c
    src/game/sys_fatfs.c
    src/game/plat_frank.c
    ${FRANK_PLATFORM_SOURCES}
)
target_include_directories(frank-war1 PRIVATE src src/usb src/game ${REPO_ROOT}/port ${FRANK_PLATFORM_DIR})
# Version: version.txt holds "MAJOR MINOR" (release.sh writes it).
file(READ ${REPO_ROOT}/version.txt FRANK_VERSION_RAW)
string(STRIP "${FRANK_VERSION_RAW}" FRANK_VERSION_RAW)
string(REGEX REPLACE "[ \t]+" ";" FRANK_VERSION_PARTS "${FRANK_VERSION_RAW}")
list(GET FRANK_VERSION_PARTS 0 FRANK_VERSION_MAJOR)
list(GET FRANK_VERSION_PARTS 1 FRANK_VERSION_MINOR)
if(FRANK_VERSION_MINOR LESS 10)
    set(FRANK_VERSION "${FRANK_VERSION_MAJOR}.0${FRANK_VERSION_MINOR}")
else()
    set(FRANK_VERSION "${FRANK_VERSION_MAJOR}.${FRANK_VERSION_MINOR}")
endif()
if(FRANK_BOARD STREQUAL "M2")
    set(FRANK_BOARD_NAME "M2")
else()
    set(FRANK_BOARD_NAME "Core 2")
endif()
message(STATUS "frank-war1 ${FRANK_VERSION} for ${FRANK_BOARD_NAME}")

target_compile_definitions(frank-war1 PRIVATE ${FRANK_DEFINITIONS}
    FRANK_WAR1_VERSION="${FRANK_VERSION}" FRANK_BOARD_NAME="${FRANK_BOARD_NAME}"
    PICO_MALLOC_PANIC=0     # war1 reports failed allocations itself
    PICO_STACK_SIZE=0x8000  # core 0 (memmap_war1.ld: STACK0)
    PICO_CORE1_STACK_SIZE=0x1000) # core 1: video and audio interrupts, SCRATCH_X
target_compile_options(frank-war1 PRIVATE ${FRANK_COPTS})
target_link_libraries(frank-war1
    pico_stdlib pico_multicore hardware_clocks hardware_dma hardware_pio
    hardware_pll hardware_vreg hardware_watchdog sdcard fatfs
    tinyusb_host tinyusb_board war1 opl)
# The game's .bss in PSRAM, the heap after it.
# libwar1 uses int-sized enums (game.cmake); nothing with an enum crosses
# into the rest (plat.h takes ints).
target_link_options(frank-war1 PRIVATE -Wl,--no-enum-size-warning)
pico_set_linker_script(frank-war1 ${CMAKE_CURRENT_LIST_DIR}/memmap_war1.ld)
# microSD on SPI0.
target_compile_definitions(frank-war1 PRIVATE
    SDCARD_SPI_BUS=spi0 SDCARD_PIN_SPI0_SCK=6 SDCARD_PIN_SPI0_MOSI=7
    SDCARD_PIN_SPI0_MISO=4 SDCARD_PIN_SPI0_CS=5)

pico_generate_pio_header(frank-war1 ${CMAKE_CURRENT_LIST_DIR}/src/audio/audio_i2s.pio)

# Console on UART0.
pico_enable_stdio_uart(frank-war1 1)
pico_enable_stdio_usb(frank-war1 0)
pico_add_extra_outputs(frank-war1)
