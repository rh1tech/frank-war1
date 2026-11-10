/*
 * frank-war1 — drawing for the firmware's own screens in Warcraft's look.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "war_fonts.h"

/* war1's font table (third_party/war1/src/war_font.c: WarFontData, its
 * rects in 1/8 pixels of the 8x atlas PNGs). */
typedef struct {
    int32_t sprite_w, sprite_h, line_height, advance;
    struct { float x, y, w, h; } data[95];
} war_font_data_t;
extern war_font_data_t fontsData[2];

static uint16_t *fb;

void ui_target(uint16_t *f) {
    fb = f;
}

uint16_t *ui_fb(void) {
    return fb;
}

static inline void px(int x, int y, uint16_t c) {
    if ((unsigned)x < UI_W && (unsigned)y < UI_H) fb[y * UI_W + x] = c;
}

void ui_fill(int x0, int y0, int x1, int y1, uint16_t c) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > UI_W) x1 = UI_W;
    if (y1 > UI_H) y1 = UI_H;
    for (int y = y0; y < y1; y++) {
        uint16_t *p = fb + y * UI_W;
        for (int x = x0; x < x1; x++) p[x] = c;
    }
}

void ui_dim_copy(const uint16_t *src) {
    /* 3/8: a half less an eighth, per channel without carries */
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *d = (uint32_t *)fb;
    for (int i = 0; i < UI_W * UI_H / 2; i++) {
        const uint32_t p = s[i];
        d[i] = ((p >> 1) & 0x7BEF7BEFu) - ((p >> 3) & 0x18E318E3u);
    }
}

/* ---- Marble ---- */

/* Computed once and copied from then on, while a screen of the firmware's
 * is up: 125 KB of the C heap (PSRAM), given back by ui_release() when the
 * game has the screen (it needs the heap: a scene alone is 1.1 MB). */
static uint16_t *marble;

static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    return h;
}

static float lattice(int x, int y) {
    return (float)(hash2(x, y) & 0xffff) * (1.0f / 65535.0f);
}

static float vnoise(float x, float y) {
    const int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = lattice(xi, yi), b = lattice(xi + 1, yi);
    const float c = lattice(xi, yi + 1), d = lattice(xi + 1, yi + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static float fbm(float x, float y) {
    float v = 0, amp = 0.5f;
    for (int o = 0; o < 4; o++) {
        v += amp * vnoise(x, y);
        x = x * 2.03f + 17.1f;
        y = y * 2.03f + 5.3f;
        amp *= 0.5f;
    }
    return v;   /* 0 .. ~0.94 */
}

static float mixf(float a, float b, float t) {
    return a + (b - a) * t;
}

static void marble_make(uint16_t *m) {
    for (int y = 0; y < UI_H; y++)
        for (int x = 0; x < UI_W; x++) {
            /* clouds: navy to slate blue */
            const float c = fbm(x / 56.0f, y / 56.0f);
            /* veins: thin bright cracks where a warped sine crosses zero */
            const float w = fbm(x / 40.0f + 9.0f, y / 40.0f + 3.0f);
            float v = 1.0f - fabsf(sinf(x * 0.017f + y * 0.011f + w * 7.0f));
            v = v * v * v * v;
            float t = c * c * 1.6f;
            if (t > 1) t = 1;
            float r = mixf(0, 26, t), g = mixf(16, 52, t), b = mixf(38, 96, t);
            r = mixf(r, 58, v * 0.30f);
            g = mixf(g, 89, v * 0.30f);
            b = mixf(b, 120, v * 0.30f);
            /* darker towards the edges, as the menu's painting */
            const float ex = (x - UI_W / 2) / (float)(UI_W / 2), ey = (y - UI_H / 2) / (float)(UI_H / 2);
            float k = 1.0f - 0.35f * (ex * ex + ey * ey);
            if (k < 0.45f) k = 0.45f;
            m[y * UI_W + x] = UI_RGB((int)(r * k), (int)(g * k), (int)(b * k));
        }
}

void ui_release(void) {
    free(marble);
    marble = NULL;
}

void ui_marble(int x0, int y0, int x1, int y1) {
    if (!marble && (marble = malloc(UI_W * UI_H * 2)) != NULL) marble_make(marble);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > UI_W) x1 = UI_W;
    if (y1 > UI_H) y1 = UI_H;
    for (int y = y0; y < y1; y++) {
        if (marble) memcpy(fb + y * UI_W + x0, marble + y * UI_W + x0, (size_t)(x1 - x0) * 2);
        else for (int x = x0; x < x1; x++) fb[y * UI_W + x] = UI_RGB(8, 30, 56);   /* no memory: plain */
    }
}

/* ---- Frames ---- */

/* One ring of a frame, the stone's two tones mottled. */
static void ring(int x0, int y0, int x1, int y1, uint16_t a, uint16_t b) {
    for (int x = x0; x < x1; x++) {
        px(x, y0, (hash2(x, y0) & 3) ? a : b);
        px(x, y1 - 1, (hash2(x, y1) & 3) ? a : b);
    }
    for (int y = y0; y < y1; y++) {
        px(x0, y, (hash2(x0, y) & 3) ? a : b);
        px(x1 - 1, y, (hash2(x1, y) & 3) ? a : b);
    }
}

void ui_box(int x0, int y0, int x1, int y1, uint16_t face, bool gold) {
    ring(x0, y0, x1, y1, gold ? UI_RGB(115, 60, 16) : UI_EDGE, gold ? UI_RGB(115, 60, 16) : UI_EDGE);
    if (gold) ring(x0 + 1, y0 + 1, x1 - 1, y1 - 1, UI_RGB(255, 219, 82), UI_GOLD_DK);
    else ring(x0 + 1, y0 + 1, x1 - 1, y1 - 1, UI_STONE, UI_STONE2);
    ring(x0 + 2, y0 + 2, x1 - 2, y1 - 2, UI_SHADE, UI_SHADE);
    if (face) ui_fill(x0 + 3, y0 + 3, x1 - 3, y1 - 3, face);
}

void ui_window(int x0, int y0, int x1, int y1) {
    /* a drop shadow onto whatever is behind */
    for (int y = y0 + 4; y < y1 + 4 && y < UI_H; y++)
        for (int x = x0 + 4; x < x1 + 4 && x < UI_W; x++)
            if (x >= x1 || y >= y1) {
                const uint16_t p = fb[y * UI_W + x];
                fb[y * UI_W + x] = (uint16_t)((p >> 2) & 0x39E7);
            }
    ui_marble(x0, y0, x1, y1);
    ring(x0, y0, x1, y1, UI_INK, UI_INK);
    ring(x0 + 1, y0 + 1, x1 - 1, y1 - 1, UI_EDGE, UI_EDGE);
    ring(x0 + 2, y0 + 2, x1 - 2, y1 - 2, UI_STONE, UI_STONE2);
    ring(x0 + 3, y0 + 3, x1 - 3, y1 - 3, UI_STONE2, UI_STONE);
    ring(x0 + 4, y0 + 4, x1 - 4, y1 - 4, UI_EDGE, UI_EDGE);
    ring(x0 + 5, y0 + 5, x1 - 5, y1 - 5, UI_SHADE, UI_SHADE);
}

/* ---- Text ---- */

static const unsigned char *atlas(int font) {
    return font ? war_font2 : war_font1;
}

static int glyph_index(char ch) {
    const unsigned u = (unsigned char)ch;
    return (u >= 32 && u < 127) ? (int)(u - 32) : '?' - 32;
}

static void glyph_rect(int font, char ch, int *gx, int *gy, int *gw) {
    const war_font_data_t *f = &fontsData[font];
    const int i = glyph_index(ch);
    *gx = (int)(f->data[i].x / 8);
    *gy = (int)(f->data[i].y / 8);
    *gw = (int)((f->data[i].w + 4) / 8);
}

int ui_text_height(int font) {
    return fontsData[font].line_height / 8;
}

static int advance(int font, char ch) {
    int gx, gy, gw;
    glyph_rect(font, ch, &gx, &gy, &gw);
    return gw + fontsData[font].advance / 8;
}

int ui_text_width(int font, const char *s) {
    int w = 0;
    for (; *s; s++) w += advance(font, *s);
    return w ? w - fontsData[font].advance / 8 : 0;
}

static void glyph(int font, int x, int y, char ch, uint16_t c) {
    int gx, gy, gw;
    glyph_rect(font, ch, &gx, &gy, &gw);
    const int h = ui_text_height(font);
    const unsigned char *a = atlas(font);
    for (int r = 0; r < h; r++)
        for (int k = 0; k < gw; k++) {
            const unsigned char v = a[(gy + r) * 64 + gx + k];
            if (v) px(x + k, y + r, v == 1 ? UI_INK : c);
        }
}

int ui_text(int font, int x, int y, const char *s, uint16_t c, int hot) {
    const int x0 = x;
    for (int i = 0; s[i]; i++) {
        glyph(font, x, y, s[i], i == hot ? UI_GOLD : c);
        x += advance(font, s[i]);
    }
    return x - x0;
}

void ui_text_center(int font, int cx, int y, const char *s, uint16_t c, int hot) {
    ui_text(font, cx - ui_text_width(font, s) / 2, y, s, c, hot);
}

void ui_text_spaced(int font, int cx, int y, const char *s, uint16_t c, int extra) {
    int w = 0;
    for (const char *p = s; *p; p++) w += advance(font, *p) + extra;
    int x = cx - (w - extra) / 2;
    for (; *s; s++) {
        glyph(font, x, y, *s, c);
        x += advance(font, *s) + extra;
    }
}

/* ---- The logo ---- */

/* The title's gold, top to bottom. */
static const uint8_t logo_ramp[][3] = {
    {255, 239, 156}, {255, 219, 99}, {255, 195, 66}, {239, 178, 60},
    {222, 162, 66}, {198, 130, 57}, {165, 105, 49}, {140, 85, 57},
};
#define LOGO_RAMP (int)(sizeof logo_ramp / sizeof logo_ramp[0])

static void logo_glyph(int x, int y, char ch, int scale, int pass) {
    int gx, gy, gw;
    glyph_rect(UI_FONT_MENU, ch, &gx, &gy, &gw);
    const int h = ui_text_height(UI_FONT_MENU);
    const unsigned char *a = atlas(UI_FONT_MENU);
    for (int r = 0; r < h; r++)
        for (int k = 0; k < gw; k++) {
            const unsigned char v = a[(gy + r) * 64 + gx + k];
            if (!v) continue;
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++) {
                    const int X = x + k * scale + dx, Y = y + r * scale + dy;
                    if (pass == 0) {
                        /* a dark outline a pixel around every glyph pixel */
                        for (int oy = -1; oy <= 1; oy++)
                            for (int ox = -1; ox <= 1; ox++) px(X + ox, Y + oy, UI_INK);
                    } else if (v == 1) {
                        px(X, Y, UI_RGB(66, 28, 8));      /* the font's shadow: bronze depth */
                    } else {
                        const int t = (r * scale + dy) * LOGO_RAMP / (h * scale);
                        const uint8_t *c = logo_ramp[t < LOGO_RAMP ? t : LOGO_RAMP - 1];
                        /* a lit upper-left edge on each letter stroke */
                        const bool lit = dx == 0 && dy == 0;
                        px(X, Y, lit ? UI_RGB(255, 247, 206) : UI_RGB(c[0], c[1], c[2]));
                    }
                }
        }
}

void ui_logo(int cx, int y) {
    static const char word[] = "WARCRAFT";
    const int big = 4, small = 3, h = ui_text_height(UI_FONT_MENU);
    int w = 0;
    for (int i = 0; word[i]; i++) {
        int gx, gy, gw;
        glyph_rect(UI_FONT_MENU, word[i], &gx, &gy, &gw);
        w += gw * ((i == 0 || i == 3) ? big : small) + 1;
    }
    for (int pass = 0; pass < 2; pass++) {
        int x = cx - w / 2;
        for (int i = 0; word[i]; i++) {
            const int s = (i == 0 || i == 3) ? big : small;
            int gx, gy, gw;
            glyph_rect(UI_FONT_MENU, word[i], &gx, &gy, &gw);
            /* the small capitals sit centred on the big ones */
            logo_glyph(x, y + (h * big - h * s) / 2, word[i], s, pass);
            x += gw * s + 1;
        }
    }
}
