/*
 * frank-war1 — the settings window (F12), after frank-castle's: sound
 * output (HDMI or I2S), volume, sound effects, music and mouse speed, kept
 * in /war1/frank.cfg.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_SETTINGS_H
#define FRANK_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

/* Read /war1/frank.cfg (the card mounted) and apply it. */
void settings_load(void);
/* F12 (or the console's `menu`): open the window at the next chance. */
void settings_request(void);
bool settings_pending(void);
bool settings_is_open(void);
/* The window, modal; returns once it is closed. `in_game`: over the game's
 * picture (the game is paused meanwhile), else over the boot shell. */
void settings_run(bool in_game);
/* A key from the console while the window is open (HID usage). */
void settings_key(uint8_t hid);

#endif
