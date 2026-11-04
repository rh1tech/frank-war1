/*
 * frank-war1 — the SDL3 subset war1 uses, on plat.h.
 *
 * The renderer draws into the platform's 320x200 RGB565 frame. war1 works
 * in float coordinates with a canvas-style transform (war_render.c); every
 * primitive here rounds to whole pixels and clips to the frame.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: Zlib
 */
#include "SDL3/SDL.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "plat.h"

#ifndef FSDL_HOT
#define FSDL_HOT
#endif

#define W PLAT_W
#define H PLAT_H

/* ------------------------------------------------------------------ init */

static const char *last_error = "";

bool SDL_Init(SDL_InitFlags flags) {
    (void)flags;
    return plat_init();
}

void SDL_Quit(void) {}

const char *SDL_GetError(void) { return last_error; }

/* ------------------------------------------------- time, random, strings */

uint64_t SDL_GetTicksNS(void) { return plat_time_ns(); }

void SDL_Delay(uint32_t ms) { plat_sleep_ms(ms); }

/* PCG32: the same quality as SDL's generator, no floating point. */
static uint64_t rng_state = 0x853c49e6748fea9bull;

static uint32_t pcg32(uint64_t *s) {
    uint64_t old = *s;
    *s = old * 6364136223846793005ull + 1442695040888963407ull;
    uint32_t x = (uint32_t)(((old >> 18) ^ old) >> 27);
    uint32_t r = (uint32_t)(old >> 59);
    return (x >> r) | (x << ((32 - r) & 31));
}

int32_t SDL_rand_r(uint64_t *state, int32_t n) {
    if (n <= 0) return 0;
    return (int32_t)(((uint64_t)pcg32(state) * (uint32_t)n) >> 32);
}

int32_t SDL_rand(int32_t n) { return SDL_rand_r(&rng_state, n); }

uint64_t fsdl_rngState(void) { return rng_state; }

float SDL_randf(void) { return (float)(pcg32(&rng_state) >> 8) * (1.0f / 16777216.0f); }

size_t SDL_strlcpy(char *dst, const char *src, size_t maxlen) {
    size_t n = strlen(src);
    if (maxlen) {
        size_t c = n < maxlen - 1 ? n : maxlen - 1;
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return n;
}

uint32_t SDL_StepUTF8(const char **pstr, size_t *pslen) {
    const unsigned char *s = (const unsigned char *)*pstr;
    size_t left = pslen ? *pslen : 4;
    if (!left || !*s) return 0;
    uint32_t c = *s;
    int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    if ((size_t)n > left) n = (int)left;
    if (n > 1) {
        c &= 0x3Fu >> (n - 1);
        for (int i = 1; i < n; i++) c = c << 6 | (s[i] & 0x3F);
    }
    *pstr += n;
    if (pslen) *pslen -= (size_t)n;
    return c;
}

/* ------------------------------------------------------------------- log */

#ifndef FSDL_LOG_MIN
#define FSDL_LOG_MIN SDL_LOG_PRIORITY_INFO
#endif
static SDL_LogPriority log_min = FSDL_LOG_MIN;

void SDL_SetLogPriority(int category, SDL_LogPriority priority) {
    (void)category;
    /* The game asks for DEBUG; never go below the build's floor. */
    log_min = priority > FSDL_LOG_MIN ? priority : FSDL_LOG_MIN;
}

void SDL_LogMessage(int category, SDL_LogPriority priority, const char *fmt, ...) {
    (void)category;
    if (priority < log_min) return;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    plat_log(priority, line);
}

/* ----------------------------------------------------------------- mutex */

SDL_Mutex *SDL_CreateMutex(void) { return (SDL_Mutex *)plat_mutex_create(); }
void SDL_DestroyMutex(SDL_Mutex *m) { if (m) plat_mutex_destroy(m); }
void SDL_LockMutex(SDL_Mutex *m) { if (m) plat_mutex_lock(m); }
void SDL_UnlockMutex(SDL_Mutex *m) { if (m) plat_mutex_unlock(m); }

/* ----------------------------------------------------------------- files */

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode) {
    FILE *f = fopen(file, mode);
    if (!f) last_error = "cannot open file";
    return (SDL_IOStream *)f;
}

int64_t SDL_GetIOSize(SDL_IOStream *s) {
    FILE *f = (FILE *)s;
    long pos = ftell(f);
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    long size = ftell(f);
    fseek(f, pos, SEEK_SET);
    return size;
}

size_t SDL_ReadIO(SDL_IOStream *s, void *ptr, size_t size) { return fread(ptr, 1, size, (FILE *)s); }
size_t SDL_WriteIO(SDL_IOStream *s, const void *ptr, size_t size) { return fwrite(ptr, 1, size, (FILE *)s); }
bool SDL_CloseIO(SDL_IOStream *s) { return fclose((FILE *)s) == 0; }

/* -------------------------------------------------------- window, misc */

struct SDL_Window { int dummy; };
struct SDL_Surface { int dummy; };
static struct SDL_Window the_window;

SDL_Window *SDL_CreateWindow(const char *title, int w, int h, uint64_t flags) {
    (void)title; (void)w; (void)h; (void)flags;
    return &the_window;
}
void SDL_DestroyWindow(SDL_Window *w) { (void)w; }
bool SDL_SetWindowSize(SDL_Window *w, int width, int height) { (void)w; (void)width; (void)height; return true; }
bool SDL_SetWindowTitle(SDL_Window *w, const char *title) { (void)w; (void)title; return true; }
bool SDL_SetWindowIcon(SDL_Window *w, SDL_Surface *icon) { (void)w; (void)icon; return true; }
bool SDL_SetWindowMouseGrab(SDL_Window *w, bool grabbed) { (void)w; (void)grabbed; return true; }
SDL_Surface *SDL_CreateSurfaceFrom(int w, int h, SDL_PixelFormat f, void *pixels, int pitch) {
    (void)w; (void)h; (void)f; (void)pixels; (void)pitch;
    return NULL;
}
void SDL_DestroySurface(SDL_Surface *s) { (void)s; }
bool SDL_HideCursor(void) { return true; }
bool SDL_StartTextInput(SDL_Window *w) { (void)w; return true; }
bool SDL_StopTextInput(SDL_Window *w) { (void)w; return true; }

/* ---------------------------------------------------------------- events */

#define EVQ 64
static SDL_Event evq[EVQ];
static unsigned evq_head, evq_tail;
static bool polled_this_frame;

static bool fsdl_push(const SDL_Event *e) {
    if (evq_head - evq_tail >= EVQ) return false;
    evq[evq_head++ % EVQ] = *e;
    return true;
}

static uint16_t hid_to_kmod(uint8_t m) {
    uint16_t k = 0;
    if (m & 0x11) k |= SDL_KMOD_CTRL;
    if (m & 0x22) k |= SDL_KMOD_SHIFT;
    if (m & 0x44) k |= SDL_KMOD_ALT;
    return k;
}

void fsdl_key(uint8_t hid, uint8_t hid_mods, bool down) {
    SDL_Event e = {0};
    e.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.key = hid;
    e.key.mod = hid_to_kmod(hid_mods);
    e.key.down = down;
    fsdl_push(&e);
}

void fsdl_text(char c) {
    SDL_Event e = {0};
    e.text.type = SDL_EVENT_TEXT_INPUT;
    e.text.text[0] = c;
    fsdl_push(&e);
}

static float mouse_x, mouse_y;

void fsdl_mouse_move(int x, int y) {
    SDL_Event e = {0};
    e.motion.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.xrel = (float)x - mouse_x;
    e.motion.yrel = (float)y - mouse_y;
    e.motion.x = mouse_x = (float)x;
    e.motion.y = mouse_y = (float)y;
    fsdl_push(&e);
}

void fsdl_mouse_button(int button, bool down, int x, int y) {
    SDL_Event e = {0};
    e.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = (uint8_t)button;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = mouse_x = (float)x;
    e.button.y = mouse_y = (float)y;
    fsdl_push(&e);
}

void fsdl_quit(void) {
    SDL_Event e = {0};
    e.type = SDL_EVENT_QUIT;
    fsdl_push(&e);
}

bool SDL_PushEvent(SDL_Event *e) {
    if (e->type == SDL_EVENT_QUIT) plat_quit();
    return fsdl_push(e);
}

bool SDL_PollEvent(SDL_Event *e) {
    if (!polled_this_frame) {
        polled_this_frame = true;
        plat_poll();
    }
    if (evq_head == evq_tail) {
        polled_this_frame = false;  /* next frame's loop polls again */
        return false;
    }
    *e = evq[evq_tail++ % EVQ];
    return true;
}

bool SDL_ConvertEventToRenderCoordinates(SDL_Renderer *r, SDL_Event *e) {
    (void)r; (void)e;   /* the platform already reports 320x200 coordinates */
    return true;
}

/* ----------------------------------------------------------------- audio */

struct SDL_AudioStream {
    SDL_AudioStreamCallback cb;
    void *userdata;
    int16_t *dst;
    int dst_left;   /* bytes */
    bool running;
};
static struct SDL_AudioStream the_stream;

SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID dev, const SDL_AudioSpec *spec,
                                           SDL_AudioStreamCallback cb, void *userdata) {
    (void)dev;
    if (spec->format != SDL_AUDIO_S16 || spec->channels != 1) {
        last_error = "only S16 mono";
        return NULL;
    }
    the_stream.cb = cb;
    the_stream.userdata = userdata;
    if (!plat_audio_open(spec->freq)) {
        last_error = "no audio";
        return NULL;
    }
    return &the_stream;
}

bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *s) {
    plat_audio_lock();
    s->running = true;
    plat_audio_unlock();
    return true;
}

bool SDL_PutAudioStreamData(SDL_AudioStream *s, const void *buf, int len) {
    if (!s->dst) return false;
    if (len > s->dst_left) len = s->dst_left;
    memcpy(s->dst, buf, (size_t)len & ~1u);
    s->dst += len / 2;
    s->dst_left -= len & ~1;
    return true;
}

void SDL_DestroyAudioStream(SDL_AudioStream *s) {
    plat_audio_lock();
    s->running = false;
    s->cb = NULL;
    plat_audio_unlock();
}

void fsdl_audio_render(int16_t *out, int frames) {
    struct SDL_AudioStream *s = &the_stream;
    int16_t *start = out;
    s->dst = out;
    s->dst_left = frames * 2;
    if (s->running && s->cb) s->cb(s->userdata, s, frames * 2, frames * 2);
    /* Whatever the callback did not fill is silence. */
    int done = (int)(s->dst - start);
    if (done < frames) memset(start + done, 0, (size_t)(frames - done) * 2);
    s->dst = NULL;
}

/* -------------------------------------------------------------- renderer */

struct SDL_Renderer {
    uint16_t *fb;
    uint8_t r, g, b, a;
    uint16_t c565;
};
static struct SDL_Renderer the_renderer;

struct SDL_Texture {
    int w, h;
    SDL_PixelFormat fmt;
    const uint8_t *px;
    const uint16_t *lut;
    int div;
    int cx, cy, cw, ch;  /* crop: px covers this part of the texture */
    uint8_t alpha;
    uint8_t mr, mg, mb;
    uint16_t tint;       /* FONT2: colour of the white pixels */
    uint16_t *px565;     /* RGBA32 with no transparent texel: the picture in
                            RGB565, made at update (the minimap) */
    bool opaque;
};

uint16_t FRANK_RGB565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

/* a in 0..32: (src * a + dst * (32 - a)) / 32, all three channels at once. */
static inline uint16_t blend565(uint16_t src, uint16_t dst, uint32_t a) {
    uint32_t s = (src | (uint32_t)src << 16) & 0x07E0F81Fu;
    uint32_t d = (dst | (uint32_t)dst << 16) & 0x07E0F81Fu;
    uint32_t m = (d + (((s - d) * a) >> 5)) & 0x07E0F81Fu;
    return (uint16_t)(m | m >> 16);
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, const char *name) {
    (void)w; (void)name;
    the_renderer.fb = plat_backbuffer();
    the_renderer.a = 255;
    return &the_renderer;
}

void SDL_DestroyRenderer(SDL_Renderer *r) { (void)r; }

bool SDL_SetRenderLogicalPresentation(SDL_Renderer *r, int w, int h, SDL_RendererLogicalPresentation mode) {
    (void)r; (void)mode;
    return w == W && h == H;
}

bool SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t red, uint8_t g, uint8_t b, uint8_t a) {
    r->r = red; r->g = g; r->b = b; r->a = a;
    r->c565 = FRANK_RGB565(red, g, b);
    return true;
}

bool SDL_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode mode) {
    (void)r; (void)mode;   /* always BLEND in war1 */
    return true;
}

bool SDL_RenderClear(SDL_Renderer *r) {
    uint32_t c = r->c565 | (uint32_t)r->c565 << 16;
    uint32_t *p = (uint32_t *)r->fb;
    for (int i = 0; i < W * H / 2; i++) p[i] = c;
    return true;
}

bool SDL_RenderPresent(SDL_Renderer *r) {
    r->fb = plat_present();
    return true;
}

/* Pixel edges of a float span, the way SDL rasterises: a pixel is covered
 * when its centre is inside. */
static inline int edge(float v) { return (int)floorf(v + 0.5f); }

static void fill_span(SDL_Renderer *r, int x0, int x1, int y0, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x0 >= x1 || y0 >= y1 || r->a == 0) return;
    const uint16_t c = r->c565;
    if (r->a == 255) {
        for (int y = y0; y < y1; y++) {
            uint16_t *p = r->fb + y * W;
            for (int x = x0; x < x1; x++) p[x] = c;
        }
    } else {
        const uint32_t a = (r->a + 4u) >> 3;
        for (int y = y0; y < y1; y++) {
            uint16_t *p = r->fb + y * W;
            for (int x = x0; x < x1; x++) p[x] = blend565(c, p[x], a);
        }
    }
}

bool SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rc) {
    if (!rc) { fill_span(r, 0, W, 0, H); return true; }
    fill_span(r, edge(rc->x), edge(rc->x + rc->w), edge(rc->y), edge(rc->y + rc->h));
    return true;
}

bool SDL_RenderRect(SDL_Renderer *r, const SDL_FRect *rc) {
    int x0 = edge(rc->x), y0 = edge(rc->y);
    int x1 = edge(rc->x + rc->w), y1 = edge(rc->y + rc->h);
    if (x1 <= x0 || y1 <= y0) return true;
    fill_span(r, x0, x1, y0, y0 + 1);
    if (y1 - 1 > y0) fill_span(r, x0, x1, y1 - 1, y1);
    fill_span(r, x0, x0 + 1, y0 + 1, y1 - 1);
    if (x1 - 1 > x0) fill_span(r, x1 - 1, x1, y0 + 1, y1 - 1);
    return true;
}

bool SDL_RenderLine(SDL_Renderer *r, float fx1, float fy1, float fx2, float fy2) {
    int x0 = (int)floorf(fx1), y0 = (int)floorf(fy1);
    int x1 = (int)floorf(fx2), y1 = (int)floorf(fy2);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fill_span(r, x0, x0 + 1, y0, y0 + 1);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return true;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, SDL_PixelFormat f, SDL_TextureAccess a, int w, int h) {
    (void)r; (void)a;
    SDL_Texture *t = (SDL_Texture *)calloc(1, sizeof(*t));
    if (!t) { last_error = "out of memory"; return NULL; }
    t->w = w; t->h = h; t->fmt = f;
    t->div = 1;
    t->cw = w; t->ch = h;
    t->alpha = 255;
    t->mr = t->mg = t->mb = 255;
    t->tint = 0xFFFF;
    return t;
}

void SDL_DestroyTexture(SDL_Texture *t) {
    if (t) free(t->px565);
    free(t);
}

bool SDL_UpdateTexture(SDL_Texture *t, const void *rect, const void *pixels, int pitch) {
    (void)rect; (void)pitch;
    t->px = (const uint8_t *)pixels;
    if (t->fmt == SDL_PIXELFORMAT_RGBA32 && pixels) {
        /* converted once here, not per texel at every draw */
        const uint8_t *q = (const uint8_t *)pixels;
        const int n = t->w * t->h;
        t->opaque = true;
        for (int i = 0; i < n && t->opaque; i++) t->opaque = q[i * 4 + 3] == 255;
        if (t->opaque && !t->px565) t->px565 = (uint16_t *)malloc((size_t)n * 2);
        if (t->opaque && t->px565)
            for (int i = 0; i < n; i++, q += 4) t->px565[i] = FRANK_RGB565(q[0], q[1], q[2]);
        else
            t->opaque = false;
    }
    return true;
}

void FRANK_SetTextureLut(SDL_Texture *t, const uint16_t *lut) { t->lut = lut; }
void FRANK_SetTextureSourceDiv(SDL_Texture *t, int div) { t->div = div > 0 ? div : 1; }
void FRANK_SetTextureCrop(SDL_Texture *t, int x, int y, int w, int h) {
    t->cx = x; t->cy = y; t->cw = w; t->ch = h;
}

bool SDL_SetTextureScaleMode(SDL_Texture *t, SDL_ScaleMode m) { (void)t; (void)m; return true; }
bool SDL_SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode m) { (void)t; (void)m; return true; }
bool SDL_SetTextureAlphaMod(SDL_Texture *t, uint8_t a) { t->alpha = a; return true; }

bool SDL_SetTextureColorMod(SDL_Texture *t, uint8_t r, uint8_t g, uint8_t b) {
    t->mr = r; t->mg = g; t->mb = b;
    t->tint = FRANK_RGB565(r, g, b);
    return true;
}

/* One texel to the frame. `a32` is the texture's alpha in 0..32. */
static inline void put(uint16_t *p, uint16_t c, uint32_t a32) {
    *p = a32 >= 32 ? c : blend565(c, *p, a32);
}

static FSDL_HOT bool render_texture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src,
                                    const SDL_FRect *dst, SDL_FlipMode flip) {
    if (!t->px || t->alpha == 0) return true;

    /* Source rectangle in texel units. */
    const float div = (float)t->div;
    float sx = src ? src->x / div : 0, sy = src ? src->y / div : 0;
    float sw = src ? src->w / div : (float)t->w, sh = src ? src->h / div : (float)t->h;
    float dxf = dst ? dst->x : 0, dyf = dst ? dst->y : 0;
    float dwf = dst ? dst->w : (float)t->w, dhf = dst ? dst->h : (float)t->h;
    if (sw <= 0 || sh <= 0 || dwf <= 0 || dhf <= 0) return true;

    /* Font atlases: war1's glyph rectangles sit on fractional texels (its
     * atlas is in 8x units). Snap them to whole texels, keeping the scale,
     * so each glyph samples exactly its own pixels at 320x200. */
    if (t->div > 1) {
        const float kx = dwf / sw, ky = dhf / sh;
        const float x0 = floorf(sx + 0.5f), x1 = floorf(sx + sw + 0.5f);
        const float y0 = floorf(sy + 0.5f), y1 = floorf(sy + sh + 0.5f);
        if (x1 <= x0 || y1 <= y0) return true;
        sx = x0; sw = x1 - x0; dwf = sw * kx;
        sy = y0; sh = y1 - y0; dhf = sh * ky;
    }

    int x0 = edge(dxf), x1 = edge(dxf + dwf);
    int y0 = edge(dyf), y1 = edge(dyf + dhf);
    if (x1 <= x0 || y1 <= y0) return true;

    /* 16.16 steps: the rounded destination rectangle covers the whole
     * source rectangle (so a glyph or sprite never loses its first column
     * to a half-pixel position), sampled at destination pixel centres. */
    const float dw = (float)(x1 - x0), dh = (float)(y1 - y0);
    const int32_t stepx = (int32_t)(sw / dw * 65536.0f);
    const int32_t stepy = (int32_t)(sh / dh * 65536.0f);
    const int32_t u0 = (int32_t)((sx + 0.5f * sw / dw) * 65536.0f);
    const int32_t v0 = (int32_t)((sy + 0.5f * sh / dh) * 65536.0f);
    const int32_t srcx0 = (int32_t)floorf(sx), srcy0 = (int32_t)floorf(sy);
    const int32_t srcx1 = (int32_t)ceilf(sx + sw), srcy1 = (int32_t)ceilf(sy + sh);
    const bool fx = flip & SDL_FLIP_HORIZONTAL, fy = flip & SDL_FLIP_VERTICAL;

    int cx0 = x0 < 0 ? 0 : x0, cx1 = x1 > W ? W : x1;
    int cy0 = y0 < 0 ? 0 : y0, cy1 = y1 > H ? H : y1;
    if (cx0 >= cx1 || cy0 >= cy1) return true;

    const uint32_t a32 = (t->alpha + 4u) >> 3;
    if (a32 == 0) return true;
    const uint8_t *px = t->px;
    const uint16_t *lut = t->lut;
    const uint16_t tint = t->tint;

    /* Fast path: an opaque RGBA picture (the minimap), 1:1, uncropped. */
    if (t->fmt == SDL_PIXELFORMAT_RGBA32 && t->opaque && t->px565 && stepx == 65536 && stepy == 65536 &&
        !fx && !fy && t->mr == 255 && t->mg == 255 && t->mb == 255 &&
        t->cx == 0 && t->cy == 0 && t->cw == t->w && t->ch == t->h) {
        const int tx0 = (u0 >> 16) + (cx0 - x0);
        int xa = cx0, xb = cx1;
        if (tx0 < 0) xa -= tx0;
        if (tx0 + (xb - cx0) > t->w) xb = cx0 + t->w - tx0;
        if (xa >= xb) return true;
        for (int y = cy0; y < cy1; y++) {
            const int ty = (v0 >> 16) + (y - y0);
            if (ty < 0 || ty >= t->h) continue;
            const uint16_t *srow = t->px565 + ty * t->w + tx0 - cx0;
            uint16_t *out = r->fb + y * W;
            if (a32 >= 32)
                memcpy(out + xa, srow + xa, (size_t)(xb - xa) * 2);
            else
                for (int x = xa; x < xb; x++) out[x] = blend565(srow[x], out[x], a32);
        }
        return true;
    }

    /* Fast path: 1:1, no flip (terrain, units, fog, glyphs at native
     * size). The crop and the clip become one span per row. */
    if (stepx == 65536 && stepy == 65536 && !fx && !fy && t->fmt != SDL_PIXELFORMAT_RGBA32) {
        const int tx0 = (u0 >> 16) + (cx0 - x0) - t->cx;     /* texel of column cx0 */
        int xa = cx0, xb = cx1;
        if (tx0 < 0) xa -= tx0;
        if (tx0 + (xb - cx0) > t->cw) xb = cx0 + t->cw - tx0;
        if (xa >= xb) return true;
        const int cw = t->cw;
        for (int y = cy0; y < cy1; y++) {
            const int ty = ((v0 >> 16) + (y - y0)) - t->cy;
            if (ty < 0 || ty >= t->ch) continue;
            const uint8_t *srow = px + ty * cw + tx0 - cx0;
            uint16_t *out = r->fb + y * W;
            if (t->fmt == SDL_PIXELFORMAT_INDEX8) {
                if (a32 >= 32) {
                    /* four texels a load (the texture is in PSRAM, behind
                     * the XIP cache: a load per texel waited on each) */
                    const uint16_t *const L = lut;
                    int x = xa;
                    for (; x < xb && ((uintptr_t)(srow + x) & 3u); x++) {
                        const uint8_t i = srow[x];
                        if (i) out[x] = L[i];
                    }
                    for (; x + 4 <= xb; x += 4) {
                        const uint32_t w = *(const uint32_t *)(srow + x);
                        if (!w) continue;
                        if (w & 0xFFu) out[x] = L[w & 0xFFu];
                        if (w & 0xFF00u) out[x + 1] = L[(w >> 8) & 0xFFu];
                        if (w & 0xFF0000u) out[x + 2] = L[(w >> 16) & 0xFFu];
                        if (w >> 24) out[x + 3] = L[w >> 24];
                    }
                    for (; x < xb; x++) {
                        const uint8_t i = srow[x];
                        if (i) out[x] = L[i];
                    }
                } else if (a32 == 16) {
                    /* the fog: half way to the texel, all channels at once */
                    for (int x = xa; x < xb; x++) {
                        const uint8_t i = srow[x];
                        if (i) {
                            const uint32_t c = lut[i], d = out[x];
                            out[x] = (uint16_t)(((c & 0xF7DEu) >> 1) + ((d & 0xF7DEu) >> 1));
                        }
                    }
                } else {
                    for (int x = xa; x < xb; x++) {
                        const uint8_t i = srow[x];
                        if (i) out[x] = blend565(lut[i], out[x], a32);
                    }
                }
            } else { /* FONT2 */
                for (int x = xa; x < xb; x++) {
                    const uint8_t i = srow[x];
                    if (i) put(&out[x], i == 1 ? 0 : tint, a32);
                }
            }
        }
        return true;
    }

    for (int y = cy0; y < cy1; y++) {
        int ty = (v0 + (y - y0) * stepy) >> 16;
        /* never sample outside the source rectangle (atlas neighbours) */
        if (ty < srcy0) ty = srcy0;
        if (ty > srcy1 - 1) ty = srcy1 - 1;
        if (fy) ty = (srcy0 + srcy1 - 1) - ty;
        ty -= t->cy;
        if (ty < 0 || ty >= t->ch) continue;
        uint16_t *out = r->fb + y * W;
        int32_t u = u0 + (cx0 - x0) * stepx;
        for (int x = cx0; x < cx1; x++, u += stepx) {
            int tx = u >> 16;
            if (tx < srcx0) tx = srcx0;
            if (tx > srcx1 - 1) tx = srcx1 - 1;
            if (fx) tx = (srcx0 + srcx1 - 1) - tx;
            tx -= t->cx;
            if (tx < 0 || tx >= t->cw) continue;
            switch (t->fmt) {
            case SDL_PIXELFORMAT_INDEX8: {
                uint8_t i = px[ty * t->cw + tx];
                if (i) put(&out[x], lut[i], a32);
                break;
            }
            case SDL_PIXELFORMAT_RGBA32: {
                const uint8_t *q = &px[(ty * t->cw + tx) * 4];
                if (q[3] == 0) break;
                uint32_t a = (q[3] * (uint32_t)t->alpha + 127) / 255;
                uint16_t c = t->mr == 255 && t->mg == 255 && t->mb == 255
                    ? FRANK_RGB565(q[0], q[1], q[2])
                    : FRANK_RGB565((uint8_t)(q[0] * t->mr / 255), (uint8_t)(q[1] * t->mg / 255),
                                   (uint8_t)(q[2] * t->mb / 255));
                put(&out[x], c, (a + 4u) >> 3);
                break;
            }
            case SDL_PIXELFORMAT_FONT2: {
                uint8_t i = px[ty * t->cw + tx];
                if (i) put(&out[x], i == 1 ? 0 : tint, a32);
                break;
            }
            }
        }
    }
    return true;
}

bool SDL_RenderTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src, const SDL_FRect *dst) {
    return render_texture(r, t, src, dst, SDL_FLIP_NONE);
}

bool SDL_RenderTextureRotated(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src, const SDL_FRect *dst,
                              double angle, const SDL_FPoint *center, SDL_FlipMode flip) {
    (void)angle; (void)center;   /* war1 only flips */
    return render_texture(r, t, src, dst, flip);
}
