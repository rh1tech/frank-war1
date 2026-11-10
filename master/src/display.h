/*
 * frank-war1 — the game's 320x200 RGB565 screen on HDMI.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Three 320x200 frame buffers in SRAM: the game draws into the back buffer
 * and display_present() swaps them at the start of the next output frame, so
 * the scanout never shows a half-drawn picture and nothing is copied. The
 * image is 640x480p60: every pixel doubled both ways, 200 rows letterboxed
 * in 240.
 */
#ifndef FRANK_DISPLAY_H
#define FRANK_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_W 320
#define DISPLAY_H 200

/* Start HDMI; the scanout interrupts run on core 1. Core 1 then calls
 * `init1` once and `idle1` in a loop (it sleeps until the next interrupt
 * when `idle1` returns false); either may be NULL. */
bool display_init(void (*init1)(void), bool (*idle1)(void));
/* Show the game's frame (display_draw565()) from the next output frame on.
 * With `wait`, return only once it is on screen. */
void display_present(bool wait);
/* The game's frame to draw next (never the one on screen or waiting). */
uint16_t *display_draw565(void);
/* Modal screens (the settings window, the boot shell): the three RGB565
 * frames by index. display_front_index() waits for a pending frame to go on
 * screen first. display_show565() puts frame `i` on screen at the next
 * output frame and returns once it is there; the game's next frame is then
 * drawn into one that is not on screen. */
int display_front_index(void);
uint16_t *display_buf565(int i);
void display_show565(int i);
/* Frames sent to HDMI so far. */
uint32_t display_frames(void);

#endif
