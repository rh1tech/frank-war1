// Ported from github.com/fboldog/rp2350-amiga src/hdmi_island.h (MPL-2.0).
// SPDX-License-Identifier: MPL-2.0
// HDMI data islands for the HSTX scanout (src/dvi_display.c): packets
// (audio samples, audio clock regeneration, InfoFrames) encoded into raw
// 30-bit HSTX words (lane 0 bits 9:0, lane 1 19:10, lane 2 29:20).
// Everything here runs in core 1's line interrupt, from RAM.
#ifndef HDMI_ISLAND_H
#define HDMI_ISLAND_H

#include <stdbool.h>
#include <stdint.h>

// One packet: header HB0..HB2 + ECC, four subpackets SB0..SB6 + ECC.
typedef struct {
    uint8_t hb[4];
    uint8_t sb[4][8];
} __attribute__((aligned(4))) hdmi_packet_t;

// Pixels of a data island with n packets: preamble, two guard bands.
#define HDMI_ISLAND_PIXELS(n) (8u + 2u + 32u * (n) + 2u)

// Fills in the header and subpacket ECC bytes (BCH).
void hdmi_packet_finish(hdmi_packet_t *p);

// Raw words, one per pixel, for blanking pixels x.. of a line. Sync levels
// (negative polarity, 1 = inactive): HSYNC is 0 for blanking pixels in
// [hsync_start, hsync_end); vsync is the line's VSYNC level.
typedef struct {
    unsigned hsync_start, hsync_end, vsync;
} hdmi_sync_t;
// Island start: 8-pixel preamble and 2-pixel leading guard band (10 words).
void hdmi_island_head(uint32_t *out, unsigned x, const hdmi_sync_t *s);
// One packet (32 words); `first` for the island's first packet.
void hdmi_encode_packet(uint32_t *out, const hdmi_packet_t *p, unsigned x,
                        bool first, const hdmi_sync_t *s);
// Island end: 2-pixel trailing guard band (2 words).
void hdmi_island_tail(uint32_t *out, unsigned x, const hdmi_sync_t *s);

// Packet builders (startup only, except hdmi_audio_packet()).
void hdmi_null_packet(hdmi_packet_t *p);
// Audio Clock Regeneration: N and CTS for the pixel clock.
void hdmi_acr_packet(hdmi_packet_t *p, uint32_t n, uint32_t cts);
// AVI InfoFrame: RGB, picture aspect 4:3, video code `vic`, RGB quantisation
// full (0..255) or limited (16..235) range.
void hdmi_avi_infoframe(hdmi_packet_t *p, uint8_t vic, bool full_range);
// Audio InfoFrame: 2-channel LPCM, rate and size from the stream.
void hdmi_audio_infoframe(hdmi_packet_t *p);
// Audio Sample packet with `count` (0..4) stereo 16-bit samples
// (left/right interleaved). `frame` is the IEC 60958 frame counter
// (0..191), advanced per sample; it sets the block start and channel
// status (48 kHz).
void hdmi_audio_packet(hdmi_packet_t *p, const int16_t *samples, int count,
                       unsigned *frame);

// Control symbol words for lanes 1/2 (lane 0 carries the syncs).
#define HDMI_TMDS_CTRL_00 0x354u
#define HDMI_TMDS_CTRL_01 0x0abu
#define HDMI_TMDS_CTRL_10 0x154u
#define HDMI_TMDS_CTRL_11 0x2abu
// Video data period preamble (CTL0..3 = 1,0,0,0) and guard band.
#define HDMI_VIDEO_PREAMBLE_L12 (HDMI_TMDS_CTRL_01 << 10 | HDMI_TMDS_CTRL_00 << 20)
#define HDMI_VIDEO_GUARD (0x2ccu | 0x133u << 10 | 0x2ccu << 20)

#endif
