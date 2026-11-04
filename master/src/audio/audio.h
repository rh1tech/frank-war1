/*
 * audio.h - the TDA1387 DAC, fed over the link.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The DAC is on this half (GPIO 9/10/11, U8), like everything else the slave
 * cannot reach. So playing a sound is: Linux -> core 1 -> link -> here -> I2S,
 * the same path as the console and the disk.
 *
 * What arrives is 16-bit signed stereo PCM, little-endian, left first -- one
 * frame per 32-bit word, which is exactly what the PIO program shifts out. It
 * lands in a ring here and the DMA completion interrupt drains it. Nothing in
 * this file blocks: the caller is the link service loop, and a loop that waits
 * for a DMA buffer is a loop that is not answering the console.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>

/* Set up PIO, DMA and pins. Silent until something is written. */
void audio_init(void);

/* Sample rate in Hz. Anything the state machine's divider can express; the
 * usual suspects are 44100, 22050 and 48000. Takes effect immediately, so it
 * is for between tracks rather than mid-buffer. */
void audio_set_rate(uint32_t hz);

/* Playback volume in 256ths, 256 being unity. Applied as the samples enter
 * the ring, so it costs the link service rather than the interrupt. The
 * default is 128: this board's output distorts before digital full scale
 * does, and half amplitude is where it stops. */
void audio_set_volume(uint32_t vol);
uint32_t audio_get_volume(void);
uint32_t audio_get_rate(void);
/* frank-amiga: DMA buffers short of samples while sound was playing. */
extern volatile uint32_t audio_starved;

/* Bytes of PCM the ring will accept right now. */
uint32_t audio_space(void);

/* Copy up to `bytes` in; returns how many were taken, which is less than asked
 * when the ring is full. Never blocks. */
uint32_t audio_write(const void *pcm, uint32_t bytes);

/* Throw away what has not been played. For a stop button, or a new track. */
void audio_flush(void);

/* Bytes still queued, so the far end can tell when a track has finished. */
uint32_t audio_queued(void);

#endif /* AUDIO_H */
