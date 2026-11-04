// Ported from github.com/fboldog/rp2350-amiga src/hdmi_island.c (MPL-2.0).
// SPDX-License-Identifier: MPL-2.0
// HDMI data islands: see hdmi_island.h. Packet layouts follow HDMI 1.4
// section 5.3 (data island period, TERC4, BCH ECC) and 5.3.4 (audio
// sample, audio clock regeneration and InfoFrame packets).
#include "hdmi_island.h"

#include "pico.h"

// TERC4: 4 data bits -> 10-bit symbol (q[9:0], sent bit 0 first). In RAM:
// read from the line interrupt.
static uint16_t __not_in_flash("hdmi") terc4[16] = {
    0x29c, 0x263, 0x2e4, 0x2e2, 0x171, 0x11e, 0x18e, 0x13c,
    0x2cc, 0x139, 0x19c, 0x2c6, 0x28e, 0x271, 0x163, 0x2c3,
};
#define GUARD_L12 0x133u  // data island guard band, lanes 1 and 2

static uint16_t __not_in_flash("hdmi") ctrl_sym[4] = {
    HDMI_TMDS_CTRL_00, HDMI_TMDS_CTRL_01, HDMI_TMDS_CTRL_10, HDMI_TMDS_CTRL_11,
};
static inline unsigned ctrl(unsigned c1, unsigned c0) {
    return ctrl_sym[(c1 << 1) | c0];
}

// BCH ECC (generator 1 + x^6 + x^7 + x^8, bits fed LSB first): a
// reflected CRC-8 with polynomial 0x83, one table lookup per byte.
static uint8_t __not_in_flash("hdmi") bch_table[256];
static bool bch_ready;

static void bch_init(void) {
    for (unsigned i = 0; i < 256u; ++i) {
        uint8_t ecc = (uint8_t)i;
        for (int bit = 0; bit < 8; ++bit)
            ecc = (uint8_t)(ecc & 1u ? (ecc >> 1) ^ 0x83u : ecc >> 1);
        bch_table[i] = ecc;
    }
    bch_ready = true;
}

static inline uint8_t bch_ecc(const uint8_t *data, int bytes) {
    uint8_t ecc = 0;
    for (int i = 0; i < bytes; ++i)
        ecc = bch_table[ecc ^ data[i]];
    return ecc;
}

// Table-driven lane 1/2 encoding (built at start-up, in RAM):
//   spread_even[v]/spread_odd[v]: the even/odd bits of subpacket byte v
//     (pixels 4b..4b+3 of byte b) as bit 0 of four nibbles;
//   pair[d1 | d2 << 4]: TERC4(d1) on lane 1 and TERC4(d2) on lane 2.
static uint16_t __not_in_flash("hdmi") spread_even[256];
static uint16_t __not_in_flash("hdmi") spread_odd[256];
static uint32_t __not_in_flash("hdmi") pair[256];
static bool tables_ready;

static void tables_init(void) {
    for (unsigned v = 0; v < 256u; ++v) {
        uint16_t e = 0, o = 0;
        for (unsigned i = 0; i < 4u; ++i) {
            e |= (uint16_t)(((v >> (2 * i)) & 1u) << (4 * i));
            o |= (uint16_t)(((v >> (2 * i + 1)) & 1u) << (4 * i));
        }
        spread_even[v] = e;
        spread_odd[v] = o;
        pair[v] = (uint32_t)terc4[v & 15u] << 10 | (uint32_t)terc4[v >> 4] << 20;
    }
    tables_ready = true;
}

void __not_in_flash_func(hdmi_packet_finish)(hdmi_packet_t *p) {
    if (!bch_ready)
        bch_init();   // first use is at start-up (hdmi_audio_prepare())
    if (!tables_ready)
        tables_init();
    p->hb[3] = bch_ecc(p->hb, 3);
    for (int i = 0; i < 4; ++i)
        p->sb[i][7] = bch_ecc(p->sb[i], 7);
}

#define HSYNC(s, px) ((px) >= (s)->hsync_start && (px) < (s)->hsync_end ? 0u : 1u)

void __not_in_flash_func(hdmi_island_head)(uint32_t *out, unsigned x,
                                           const hdmi_sync_t *s) {
    // Preamble: control period with CTL0..3 = 1,0,1,0.
    for (int i = 0; i < 8; ++i, ++x)
        *out++ = ctrl(s->vsync, HSYNC(s, x)) |
                 HDMI_TMDS_CTRL_01 << 10 | HDMI_TMDS_CTRL_01 << 20;
    // Leading guard band: lane 0 TERC4 {1, 1, VSYNC, HSYNC}.
    for (int i = 0; i < 2; ++i, ++x)
        *out++ = terc4[0xcu | s->vsync << 1 | HSYNC(s, x)] |
                 GUARD_L12 << 10 | GUARD_L12 << 20;
}

void __not_in_flash_func(hdmi_island_tail)(uint32_t *out, unsigned x,
                                           const hdmi_sync_t *s) {
    for (int i = 0; i < 2; ++i, ++x)
        *out++ = terc4[0xcu | s->vsync << 1 | HSYNC(s, x)] |
                 GUARD_L12 << 10 | GUARD_L12 << 20;
}

void __not_in_flash_func(hdmi_encode_packet)(uint32_t *out,
                                             const hdmi_packet_t *p,
                                             unsigned x, bool first,
                                             const hdmi_sync_t *s) {
    (void)first;
    uint32_t header = (uint32_t)p->hb[0] | (uint32_t)p->hb[1] << 8 |
                      (uint32_t)p->hb[2] << 16 | (uint32_t)p->hb[3] << 24;
    // Per byte b: lane 1 and lane 2 nibbles of pixels 4b..4b+3; subpacket k
    // is bit k of each nibble.
    uint16_t e[8], o[8];
    for (int b = 0; b < 8; ++b) {
        e[b] = (uint16_t)(spread_even[p->sb[0][b]] |
                          spread_even[p->sb[1][b]] << 1 |
                          spread_even[p->sb[2][b]] << 2 |
                          spread_even[p->sb[3][b]] << 3);
        o[b] = (uint16_t)(spread_odd[p->sb[0][b]] |
                          spread_odd[p->sb[1][b]] << 1 |
                          spread_odd[p->sb[2][b]] << 2 |
                          spread_odd[p->sb[3][b]] << 3);
    }
    const unsigned base0 = s->vsync << 1 | 8u;   // bit 3 is set in islands
    for (unsigned b = 0; b < 8u; ++b) {
        unsigned eb = e[b], ob = o[b];
        for (unsigned i = 0; i < 4u; ++i, ++x) {
            const unsigned hsync =
                x >= s->hsync_start && x < s->hsync_end ? 0u : 1u;
            *out++ = terc4[hsync | base0 | (header & 1u) << 2] |
                     pair[(eb & 15u) | (ob & 15u) << 4];
            header >>= 1;
            eb >>= 4;
            ob >>= 4;
        }
    }
}

// Nine explicit stores: a loop here becomes a memset() call, which lives
// in flash (this runs in the line interrupt).
static inline void clear(hdmi_packet_t *p) {
    _Static_assert(sizeof(*p) == 36, "packet layout");
    volatile uint32_t *w = (volatile uint32_t *)p;
    w[0] = 0; w[1] = 0; w[2] = 0; w[3] = 0; w[4] = 0;
    w[5] = 0; w[6] = 0; w[7] = 0; w[8] = 0;
}

void hdmi_null_packet(hdmi_packet_t *p) {
    clear(p);
    hdmi_packet_finish(p);
}

void hdmi_acr_packet(hdmi_packet_t *p, uint32_t n,
                                          uint32_t cts) {
    clear(p);
    p->hb[0] = 0x01;
    for (int s = 0; s < 4; ++s) {
        p->sb[s][1] = (uint8_t)(cts >> 16 & 0x0f);
        p->sb[s][2] = (uint8_t)(cts >> 8);
        p->sb[s][3] = (uint8_t)cts;
        p->sb[s][4] = (uint8_t)(n >> 16 & 0x0f);
        p->sb[s][5] = (uint8_t)(n >> 8);
        p->sb[s][6] = (uint8_t)n;
    }
    hdmi_packet_finish(p);
}

// InfoFrame: payload bytes PB1..PBlen; PB0 is the checksum. Subpacket s
// holds PB(7s)..PB(7s+6).
static void infoframe(hdmi_packet_t *p, uint8_t type,
                                           uint8_t version, uint8_t len,
                                           const uint8_t *pb) {
    clear(p);
    p->hb[0] = type;
    p->hb[1] = version;
    p->hb[2] = len;
    uint8_t bytes[28] = {0};
    unsigned sum = type + version + len;
    for (unsigned i = 1; i <= len; ++i) {
        bytes[i] = pb[i - 1];
        sum += pb[i - 1];
    }
    bytes[0] = (uint8_t)(0x100u - (sum & 0xffu));
    for (int s = 0; s < 4; ++s)
        for (int b = 0; b < 7; ++b)
            p->sb[s][b] = bytes[7 * s + b];
    hdmi_packet_finish(p);
}

void hdmi_avi_infoframe(hdmi_packet_t *p, uint8_t vic, bool full_range) {
    const uint8_t pb[13] = {
        0x00,        // RGB, no active format / bar / scan info
        0x18,        // picture aspect 4:3, active format = picture
        full_range ? 0x08 : 0x04,   // RGB quantisation: full / limited
        vic,
        0x00,        // no pixel repetition
    };
    infoframe(p, 0x82, 0x02, 13, pb);
}

void hdmi_audio_infoframe(hdmi_packet_t *p) {
    const uint8_t pb[10] = {
        0x01,        // coding: refer to stream; 2 channels
        0x00,        // rate and size: refer to stream
        0x00,
        0x00,        // speaker allocation: front left/right
        0x00,
    };
    infoframe(p, 0x84, 0x01, 10, pb);
}

void __not_in_flash_func(hdmi_audio_packet)(hdmi_packet_t *p,
                                            const int16_t *samples,
                                            int count, unsigned *frame) {
    clear(p);
    p->hb[0] = 0x02;
    p->hb[1] = (uint8_t)((1u << count) - 1u);  // layout 0, samples present
    for (int s = 0; s < count; ++s) {
        const unsigned f = *frame;
        *frame = f + 1u == 192u ? 0u : f + 1u;
        if (f == 0u)
            p->hb[2] |= (uint8_t)(0x10u << s);  // B: IEC 60958 block start
        // Channel status: consumer, LPCM, 48 kHz (bit 25), same for both.
        const unsigned cs = f == 25u ? 1u : 0u;
        uint8_t *sb = p->sb[s];
        for (int ch = 0; ch < 2; ++ch) {
            const uint32_t v = (uint32_t)(int32_t)samples[2 * s + ch] << 8 &
                               0xffffffu;
            sb[3 * ch] = (uint8_t)v;
            sb[3 * ch + 1] = (uint8_t)(v >> 8);
            sb[3 * ch + 2] = (uint8_t)(v >> 16);
            // V = 0, U = 0, C, P: even parity over sample, V, U, C.
            const unsigned parity =
                ((unsigned)__builtin_parity(v) ^ cs) & 1u;
            sb[6] |= (uint8_t)((cs << 2 | parity << 3) << (4 * ch));
        }
    }
    hdmi_packet_finish(p);
}
