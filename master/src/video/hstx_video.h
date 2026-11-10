/*
 * frank-blood — HSTX DVI scanout with a per-row callback.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_HSTX_VIDEO_H
#define FRANK_HSTX_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

/* Both at 25.2 MHz pixels (TMDS 252 MHz, clk_hstx = 504 MHz / 4), 240
 * source rows. 720-wide CEA modes need 27 MHz, which 504 MHz cannot make. */
typedef enum {
    VIDEO_640x480p50,   /* VGA timing, long vertical blank: PAL rate */
    VIDEO_640x480p60,   /* VGA (CEA VIC 1) */
    VIDEO_640x512p50,   /* 25.2 MHz too, 630 lines: all 256 PAL rows at 2x */
    VIDEO_MODE_COUNT,
} video_mode_t;

/* Fill source row `row` with `width` RGB888 pixels (0x00RRGGBB). `field`
 * is 2 for a progressive row (shown on two output lines), or 0/1 while
 * weaving: the row of that interlace field, shown on one line. Runs in an interrupt on the core that called
 * video_start(), roughly two output lines before the row is sent; must live
 * in RAM. */
typedef void (*video_row_fn)(uint32_t row, uint32_t field, uint32_t *rgb888,
                             uint32_t width);
/* Called from the scanout interrupt at the start of every output frame
 * (line 0). Optional. */
typedef void (*video_frame_fn)(uint32_t frame);

/* Retune PLL_USB for the mode and claim resources. Core-agnostic. */
bool video_init(video_mode_t mode, video_row_fn row, video_frame_fn frame);
/* Start scanout; its interrupts run on the calling core. */
void video_start(void);

/* Weave interlaced fields (takes effect per line; switch it at line 0). */
void video_set_weave(bool on);
uint32_t video_width(void);
uint32_t video_rows(void);
const char *video_mode_name(void);
/* RGB quantisation range of the mode (both 640x480 modes: full). */
bool video_full_range(void);
/* Amiga colour to RGB888 in the active mode's range. Call after
 * video_init(); build lookup tables from these rather than per pixel. */
uint8_t video_nibble_to_8(uint32_t nibble);
uint32_t video_rgb12_to_888(uint16_t rgb12);

/* HDMI audio: 44.1 kHz stereo 16-bit in audio sample packets (off until
 * enabled). Push takes interleaved left/right frames; what does not fit is
 * dropped (counted). Space: the frames the ring takes now. */
#define VIDEO_AUDIO_RATE 44100u
void video_audio_enable(bool on);
bool video_audio_enabled(void);
uint32_t video_audio_space(void);
void video_audio_push(const int16_t *lr, uint32_t frames);
extern volatile uint32_t video_audio_underruns, video_audio_drops;

extern volatile uint32_t video_frames;
extern volatile uint32_t video_late_rows;

#endif
