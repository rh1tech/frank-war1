# frank-war1 — firmware (RP2350B): Warcraft: Orcs & Humans on one chip.
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.13)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PICO_BOARD_HEADER_DIRS ${REPO_ROOT}/common/boards CACHE PATH "" FORCE)
include(${REPO_ROOT}/common/frank.cmake)
# FRANK_BOARD (frank.cmake): CORE2 -> FRANK Core 2 master, M2 -> Murmulator /
# FRANK M2 (frank-quake2's M2 board definition, the same peripheral pins).
if(FRANK_BOARD STREQUAL "M2")
    set(PICO_BOARD "frank_m2_rp2350b" CACHE STRING "Pico board type" FORCE)
    set(FRANK_BOARD_NAME "M2")
elseif(FRANK_BOARD STREQUAL "CORE2")
    set(PICO_BOARD "frank_core2_master" CACHE STRING "Pico board type" FORCE)
    set(FRANK_BOARD_NAME "Core 2")
else()
    message(FATAL_ERROR "FRANK_BOARD must be CORE2 or M2")
endif()
include(${REPO_ROOT}/pico_sdk_import.cmake)
project(frank-war1 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(lib/fatfs)
include(lib/sdcard/sdcard.cmake)
include(game.cmake)

# Native USB: the keyboard/mouse/gamepad host (release builds), or with
# -DUSB_HID=OFF a USB CDC console for development (frank-castle; no
# keyboard then, the console's key/mouse commands drive the game; "~BOOT~"
# reboots into BOOTSEL). UART0 (GPIO0/1) carries the console as well.
option(USB_HID "Native USB is the keyboard/gamepad host (OFF: USB CDC console)" ON)
if(USB_HID)
    set(FRANK_USB_SOURCES src/usb/hid_app.c src/usb/xinput_host.c)
    set(FRANK_USB_LIBS tinyusb_host tinyusb_board)
    set(FRANK_USB_INCLUDE src/usb)
else()
    set(FRANK_USB_SOURCES src/usb/usbhid_stub.c)
    set(FRANK_USB_LIBS)
    set(FRANK_USB_INCLUDE)   # src/usb's tusb_config.h is the host's
endif()

add_executable(frank-war1
    src/main.c
    src/console.c
    src/display.c
    src/shell.c
    src/settings.c
    src/ui/ui.c
    src/video/hstx_video.c
    src/video/hdmi_island.c
    src/audio/audio.c
    ${FRANK_USB_SOURCES}
    src/game/sys_fatfs.c
    src/game/plat_frank.c
    ${FRANK_PLATFORM_SOURCES}
)
target_include_directories(frank-war1 PRIVATE src ${FRANK_USB_INCLUDE} src/game ${REPO_ROOT}/port ${FRANK_PLATFORM_DIR})
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
    ${FRANK_USB_LIBS} war1 opl)
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

# Console on UART0, and on the USB CDC port when the USB is not the host.
pico_enable_stdio_uart(frank-war1 1)
if(USB_HID)
    pico_enable_stdio_usb(frank-war1 0)
else()
    pico_enable_stdio_usb(frank-war1 1)
    # printf must never wait for a terminal that is not reading
    target_compile_definitions(frank-war1 PRIVATE FRANK_USB_CONSOLE=1
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=0)
endif()
pico_add_extra_outputs(frank-war1)
