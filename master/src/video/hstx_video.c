/*
 * frank-blood — HSTX DVI scanout with a per-row callback.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Structure follows the HSTX scanout of github.com/fboldog/rp2350-amiga
 * (src/dvi_display.c, MPL-2.0): four DMA channels in a ring
 * (blanking p -> active p -> blanking p^1 ...), the blanking interrupt two
 * lines ahead, and row expansion in a lower-priority interrupt so a slow
 * row can only tear, never desynchronise the ring.
 *
 * Every source row is shown on two output lines (576 -> 288 rows,
 * 480 -> 240). Rows are RGB888, one pixel per 32-bit word (0x00RRGGBB), as
 * wide as the active line; the callback fills the whole width including any
 * border.
 *
 * Board: FRANK Core 2 master, GPIO12..19 with each pair's N on the lower
 * pin (CLK 12/13, D0 blue 14/15, D1 green 16/17, D2 red 18/19).
 */
#include "hstx_video.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/structs/busctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/nvic.h"
#include "pico/stdlib.h"

#include "clocks.h"
#include "hdmi_island.h"

#define PIN_CLK_N 12u
#define PIN_D0_N  14u
#define PIN_D1_N  16u
#define PIN_D2_N  18u

typedef struct {
    const char *name;
    uint16_t width, height;          /* active output pixels / lines */
    uint16_t hfp, hsync, hbp;
    uint16_t vfp, vsync, vbp;
    uint32_t bit_khz;                /* TMDS bit rate = 10 x pixel clock */
    uint8_t  vic;
    bool     full_range;     /* IT format; CEA formats default to limited */
} mode_t;

static const mode_t modes[] = {
    /* VGA timing; the 50 Hz variant stretches the vertical back porch so
     * PAL frames land one per refresh (25.2 MHz / (800 x 630) = 50.0 Hz). */
    [VIDEO_640x480p50] = { "640x480p50", 640, 480, 16, 96, 48, 10, 2, 138, 252000, 0, true },
    [VIDEO_640x480p60] = { "640x480p60", 640, 480, 16, 96, 48, 10, 2, 33, 252000, 1, true },
    /* frank-amiga: the PAL image's 256 rows whole (overscan screens such as
     * SWOS's menus reach below 640x480's 240): the same clock and 630-line
     * frame, 512 of them active. Not a CEA format; monitors take it, a TV
     * may not (display=480 in the ini falls back). */
    [VIDEO_640x512p50] = { "640x512p50", 640, 512, 16, 96, 48, 10, 2, 106, 252000, 0, true },
};

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu
#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

#define MAX_WIDTH 640u
#define ROW_BUFFERS 3u
/* [0] = TMDS | width, then width pixel words, then a NOP. */
#define ROW_WORDS (1u + MAX_WIDTH + 1u)

static const mode_t *m;
static uint32_t v_total, h_blank, rows;
static video_row_fn row_fn;
static video_frame_fn frame_fn;

static uint32_t row_buf[ROW_BUFFERS][ROW_WORDS] __attribute__((aligned(4)));
static uint32_t vblank_active[3], vsync_active[3];

/* HDMI, not DVI, so the AVI InfoFrame can state the RGB range. CEA formats
 * (576p, 480p) are sent limited-range (16..235): that is their default, and
 * the bench capture card applies it regardless of the InfoFrame, as many
 * TVs do. 640x480 (an IT format) is full range. Every line's blanking carries one data
 * island (the AVI InfoFrame on line 0, a null packet elsewhere), and active
 * lines end their blanking with the video preamble and guard band.
 *
 * The blanking depends only on (VSYNC level, active line, line 0), so all
 * eight variants are built once and the line interrupt just points the
 * DMA at one. */
#define ISLAND_X 4u
#define HBLANK_WORDS 80u
static uint32_t hblank_var[8][HBLANK_WORDS];
static uint8_t hblank_len[8];

static uint hstx_dma[4];
static uint hblank_line[2], active_line[2];
/* Expansion requests per row buffer: -1, or row | field << 16 (field 0/1
 * while weaving interlaced fields, 2 for a progressive row). */
static volatile int expand_row[ROW_BUFFERS] = { -1, -1, -1 };
static volatile bool weave;
void video_set_weave(bool on) { weave = on; }

/* Row buffer and request of output line `line` (< height). Progressive:
 * one row per two lines, buffers alternate per row. Weaving: every line its
 * own row of field line & 1; three buffers so the one being sent is never
 * the one being filled. */
static inline uint line_buffer(uint line) {
    return weave ? line % ROW_BUFFERS : (line >> 1) & 1u;
}
static inline int line_request(uint line) {
    return weave ? (int)((line >> 1) | (line & 1u) << 16) : (int)((line >> 1) | 2u << 16);
}
static uint expand_irq;

volatile uint32_t video_frames;
volatile uint32_t video_late_rows;

static inline bool line_is_vsync(uint line) {
    return line >= m->height + m->vfp && line < m->height + m->vfp + m->vsync;
}

static inline uint32_t sync_word(uint v, uint h) {
    return v ? (h ? SYNC_V1_H1 : SYNC_V1_H0) : (h ? SYNC_V0_H1 : SYNC_V0_H0);
}

/* Variant key: bit 0 = VSYNC inactive, bit 1 = active line, bit 2 = line 0. */
static void build_hblank(uint key) {
    uint32_t *w = hblank_var[key];
    const uint v = key & 1u;                          /* active-low syncs */
    const bool active = key & 2u;
    const hdmi_sync_t sync = { m->hfp, m->hfp + m->hsync, v };
    hdmi_packet_t pk;
    if (key & 4u) hdmi_avi_infoframe(&pk, m->vic, m->full_range);
    else hdmi_null_packet(&pk);

    *w++ = HSTX_CMD_RAW_REPEAT | ISLAND_X;
    *w++ = sync_word(v, 1);
    *w++ = HSTX_CMD_RAW | HDMI_ISLAND_PIXELS(1);
    hdmi_island_head(w, ISLAND_X, &sync);                    w += 10;
    hdmi_encode_packet(w, &pk, ISLAND_X + 10u, true, &sync); w += 32;
    hdmi_island_tail(w, ISLAND_X + 10u + 32u, &sync);        w += 2;
    uint x = ISLAND_X + HDMI_ISLAND_PIXELS(1);
    const uint end = active ? h_blank - 10u : h_blank;
    while (x < end) {
        const uint hs_start = m->hfp, hs_end = m->hfp + m->hsync;
        const uint h = (x >= hs_start && x < hs_end) ? 0u : 1u;
        uint next = h ? (x < hs_start ? hs_start : end) : hs_end;
        if (next > end) next = end;
        *w++ = HSTX_CMD_RAW_REPEAT | (next - x);
        *w++ = sync_word(v, h);
        x = next;
    }
    if (active) {
        *w++ = HSTX_CMD_RAW_REPEAT | 8u;
        *w++ = (sync_word(v, 1) & 0x3ffu) | HDMI_VIDEO_PREAMBLE_L12;
        *w++ = HSTX_CMD_RAW_REPEAT | 2u;
        *w++ = HDMI_VIDEO_GUARD;
    }
    *w++ = HSTX_CMD_NOP;
    hblank_len[key] = (uint8_t)(w - hblank_var[key]);
}

static inline uint plus2(uint line) {
    line += 2u;
    return line >= v_total ? line - v_total : line;
}

static void __not_in_flash_func(next_hblank)(uint p) {
    const uint line = hblank_line[p];
    hblank_line[p] = plus2(line);
    if (line == 0u) {
        video_frames++;
        if (frame_fn) frame_fn(video_frames);
    }
    /* The line this channel just finished is now sending its active part:
     * if that row is still waiting for expansion, it is genuinely late. */
    const uint sent = line >= 2u ? line - 2u : line + v_total - 2u;
    if (sent < m->height && expand_row[line_buffer(sent)] == line_request(sent))
        video_late_rows++;
    const uint key = (line_is_vsync(line) ? 0u : 1u) |
                     (line < m->height ? 2u : 0u) | (line == 0u ? 4u : 0u);
    dma_channel_hw_t *ch = &dma_hw->ch[hstx_dma[p]];
    ch->read_addr = (uintptr_t)hblank_var[key];
    ch->transfer_count = hblank_len[key];
    /* Expand the line's row now, nearly two lines before it is sent: every
     * line while weaving, else the first line of each row. */
    if (line < m->height && (weave || !(line & 1u))) {
        expand_row[line_buffer(line)] = line_request(line);
        /* frank-blood: irq_set_pending() lives in flash; this interrupt must
         * not wait for XIP while the game on the other core saturates it. */
        nvic_hw->ispr[expand_irq / 32u] = 1u << (expand_irq % 32u);
    }
}

static void __not_in_flash_func(next_active)(uint p) {
    const uint line = active_line[p];
    active_line[p] = plus2(line);
    const uint32_t *list;
    uint count;
    if (line < m->height) {
        list = row_buf[line_buffer(line)];
        count = 2u + m->width;
    } else if (line_is_vsync(line)) {
        list = vsync_active;
        count = 3;
    } else {
        list = vblank_active;
        count = 3;
    }
    dma_channel_hw_t *ch = &dma_hw->ch[hstx_dma[2u + p]];
    ch->read_addr = (uintptr_t)list;
    ch->transfer_count = count;
}

static void __not_in_flash_func(dma_irq)(void) {
    for (uint p = 0; p < 2u; ++p) {
        const uint32_t a = 1u << hstx_dma[2u + p];
        if (dma_hw->ints1 & a) { dma_hw->ints1 = a; next_active(p); }
        const uint32_t b = 1u << hstx_dma[p];
        if (dma_hw->ints1 & b) { dma_hw->ints1 = b; next_hblank(p); }
    }
}

static void __not_in_flash_func(expand_isr)(void) {
    bool again = true;
    while (again) {
        again = false;
        for (uint b = 0; b < ROW_BUFFERS; ++b) {
            const int req = expand_row[b];
            if (req < 0) continue;
            row_fn((uint32_t)req & 0xFFFFu, (uint32_t)req >> 16, &row_buf[b][1], m->width);
            expand_row[b] = -1;
            again = true;
        }
    }
}

static void hstx_configure(void) {
    /* Two bits per clk_hstx cycle (DDR): a 10-bit symbol every 5 cycles. */
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS |
                        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
                        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
                        2u << HSTX_CTRL_CSR_SHIFT_LSB |
                        HSTX_CTRL_CSR_EN_BITS;
    /* RGB888, one pixel per word: red 23:16 -> lane 2, green 15:8 ->
     * lane 1, blue 7:0 -> lane 0. */
    hstx_ctrl_hw->expand_tmds =
        7u << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB | 16u << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
        7u << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB | 8u << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
        7u << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB | 0u << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
    hstx_ctrl_hw->expand_shift =
        1u << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        0u << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0u << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
    /* HSTX output bit n is GPIO 12+n. Each pair: N on the even bit
     * (inverted), P on the odd bit. */
    hstx_ctrl_hw->bit[PIN_CLK_N - 12u] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    hstx_ctrl_hw->bit[PIN_CLK_N - 11u] = HSTX_CTRL_BIT0_CLK_BITS;
    const uint lane_n[3] = { PIN_D0_N, PIN_D1_N, PIN_D2_N };
    for (uint lane = 0; lane < 3u; ++lane) {
        const uint32_t sel = (lane * 10u) << HSTX_CTRL_BIT0_SEL_P_LSB |
                             (lane * 10u + 1u) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[lane_n[lane] - 12u] = sel | HSTX_CTRL_BIT0_INV_BITS;
        hstx_ctrl_hw->bit[lane_n[lane] - 11u] = sel;
    }
    for (uint pin = 12u; pin <= 19u; ++pin)
        gpio_set_function(pin, GPIO_FUNC_HSTX);
}

bool video_init(video_mode_t mode, video_row_fn rfn, video_frame_fn ffn) {
    if (mode >= VIDEO_MODE_COUNT || !rfn) return false;
    m = &modes[mode];
    row_fn = rfn;
    frame_fn = ffn;
    h_blank = m->hfp + m->hsync + m->hbp;
    v_total = m->height + m->vfp + m->vsync + m->vbp;
    rows = m->height / 2u;
    if (!frank_clocks_set_tmds(m->bit_khz)) return false;
    reset_unreset_block_num_wait_blocking(RESET_HSTX);
    for (uint i = 0; i < 4u; ++i)
        hstx_dma[i] = (uint)dma_claim_unused_channel(true);
    vblank_active[0] = HSTX_CMD_RAW_REPEAT | m->width;
    vblank_active[1] = SYNC_V1_H1;
    vblank_active[2] = HSTX_CMD_NOP;
    vsync_active[0] = HSTX_CMD_RAW_REPEAT | m->width;
    vsync_active[1] = SYNC_V0_H1;
    vsync_active[2] = HSTX_CMD_NOP;
    for (uint b = 0; b < ROW_BUFFERS; ++b) {
        row_buf[b][0] = HSTX_CMD_TMDS | m->width;
        row_buf[b][1u + m->width] = HSTX_CMD_NOP;
    }
    for (uint key = 0; key < 8u; ++key)
        build_hblank(key);
    return true;
}

void video_start(void) {
    expand_irq = (uint)user_irq_claim_unused(true);
    irq_set_exclusive_handler(expand_irq, expand_isr);
    irq_set_priority(expand_irq, 0xc0);
    irq_set_enabled(expand_irq, true);
    for (uint p = 0; p < 2u; ++p)
        hblank_line[p] = active_line[p] = p;
    for (uint p = 0; p < 2u; ++p) {
        const uint chans[2][2] = {
            { hstx_dma[p], hstx_dma[2u + p] },
            { hstx_dma[2u + p], hstx_dma[p ^ 1u] },
        };
        for (uint i = 0; i < 2u; ++i) {
            dma_channel_config c = dma_channel_get_default_config(chans[i][0]);
            channel_config_set_chain_to(&c, chans[i][1]);
            channel_config_set_dreq(&c, DREQ_HSTX);
            dma_channel_configure(chans[i][0], &c, &hstx_fifo_hw->fifo,
                                  vblank_active, 3, false);
        }
    }
    /* Rows 0 and 1 before the first lines go out. */
    row_fn(0, 2, &row_buf[0][1], m->width);
    for (uint p = 0; p < 2u; ++p) {
        next_hblank(p);
        next_active(p);
    }
    uint32_t mask = 0;
    for (uint i = 0; i < 4u; ++i) mask |= 1u << hstx_dma[i];
    dma_hw->ints1 = mask;
    dma_hw->inte1 = mask;
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq);
    irq_set_priority(DMA_IRQ_1, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
    /* Scanout DMA wins bus arbitration over both cores. */
    hw_set_bits(&busctrl_hw->priority,
                BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS);
    hstx_configure();
    dma_channel_start(hstx_dma[0]);
}

uint32_t video_width(void) { return m ? m->width : 0; }
uint32_t video_rows(void) { return rows; }
const char *video_mode_name(void) { return m ? m->name : "none"; }
bool video_full_range(void) { return m ? m->full_range : true; }

/* One 4-bit Amiga colour component -> 8 bits in the mode's range: full
 * range replicates the nibble (0xF -> 0xFF); limited range maps 0..15 onto
 * 16..235, as CEA modes require. */
uint8_t video_nibble_to_8(uint32_t n) {
    n &= 15u;
    return (uint8_t)((m && !m->full_range) ? 16u + (n * 219u + 7u) / 15u : n * 17u);
}

uint32_t video_rgb12_to_888(uint16_t c) {
    return (uint32_t)video_nibble_to_8(c >> 8) << 16 |
           (uint32_t)video_nibble_to_8(c >> 4) << 8 | video_nibble_to_8(c);
}
