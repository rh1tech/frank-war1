/*
 * frank-war1 — what the game needs from a platform. port/fsdl.c builds the
 * SDL3 subset on top of this; host/plat_host.c and master/src/game/plat_frank.c
 * implement it.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: Zlib
 */
#ifndef FRANK_PLAT_H
#define FRANK_PLAT_H

#include <stdbool.h>
#include <stdint.h>

#define PLAT_W 320
#define PLAT_H 200

#ifdef __cplusplus
extern "C" {
#endif

/* Video: a 320x200 RGB565 frame the game draws into; present shows it and
 * returns the next one to draw (its contents are undefined: war1 clears and
 * redraws every frame). */
bool plat_init(void);
uint16_t *plat_backbuffer(void);
uint16_t *plat_present(void);

/* Time since start and a coarse sleep. */
uint64_t plat_time_ns(void);
void plat_sleep_ms(uint32_t ms);

/* Input: called once per frame from SDL_PollEvent; the platform reports
 * what happened through the fsdl_* calls below, in 320x200 coordinates. */
void plat_poll(void);

/* Audio: the platform calls fsdl_audio_render() for `frames` mono 16-bit
 * samples at the rate plat_audio_open() was given (from its audio thread or
 * core). Music is mixed by the platform, outside the game's stream. */
bool plat_audio_open(int freq);
void plat_audio_lock(void);
void plat_audio_unlock(void);

/* Locks for SDL_Mutex. */
void *plat_mutex_create(void);
void plat_mutex_lock(void *m);
void plat_mutex_unlock(void *m);
void plat_mutex_destroy(void *m);

void plat_log(int priority, const char *line);
/* The game asked to quit. */
void plat_quit(void);

/* fsdl side, for the platform. Keys are USB HID usages; mod uses the
 * HID modifier byte (bit 0/4 ctrl, 1/5 shift, 2/6 alt). Buttons: 1 left,
 * 2 middle, 3 right. */
void fsdl_key(uint8_t hid, uint8_t hid_mods, bool down);
void fsdl_text(char c);
void fsdl_mouse_move(int x, int y);
void fsdl_mouse_button(int button, bool down, int x, int y);
void fsdl_quit(void);
void fsdl_audio_render(int16_t *out, int frames);

#ifdef __cplusplus
}
#endif
#endif
