/*
 * frank-war1 — the game's 320x200 8-bit screen on HDMI.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Three 320x200 frame buffers in SRAM (two in the boot shell): the game draws into the back buffer and
 * display_present() swaps them at the start of the next output frame, so the
 * scanout never shows a half-drawn picture and nothing is copied. The image
 * is 640x480p60: every pixel doubled both ways, 200 rows letterboxed in 240.
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
/* The buffer to draw the next frame into. */
uint8_t *display_backbuffer(void);
/* Show the back buffer from the next frame on. With `wait`, return only once
 * it is on screen (the old front buffer is then free to draw into). */
void display_present(bool wait);
/* Show the back buffer from the next row on (no waiting; a frame may show a
 * tear) and return the new back buffer, which held the previous picture. */
uint8_t *display_flip(void);
/* Palette entries as 8-bit R, G, B triplets; takes effect at the next frame. */
void display_set_palette(const uint8_t *rgb, int first, int count);
/* The buffer on screen now (the boot shell draws into it directly). */
uint8_t *display_front(void);
/* Called on core 1 at the start of every output frame (interrupt context,
 * must live in RAM), NULL for none. */
void display_set_vsync_hook(void (*hook)(void));
/* The game's 16-bit RGB565 frames instead of the shell's 8-bit indexed
 * ones (same two buffers); takes effect from the next row. */
void display_set_rgb565(bool on);
uint16_t *display_backbuffer565(void);
/* The game's frame to draw next (never the one on screen or waiting). */
uint16_t *display_draw565(void);
/* Like display_flip() for RGB565 frames. */
uint16_t *display_flip565(void);
/* Frames sent to HDMI so far. */
uint32_t display_frames(void);

#endif
