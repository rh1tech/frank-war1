/*
 * frank-war1 — the settings window (F12), after frank-castle's settings
 * window, drawn as one of Warcraft's menu dialogs: a marble window in a
 * stone frame, values in stone-framed boxes, the selected one framed in gold.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Modal: the game waits inside plat_poll() while it is open (its clock and
 * sound paused), its last picture dimmed behind the window. Every change is
 * drawn into a frame that is not on screen and swapped in at the start of an
 * output frame; nothing is redrawn while nothing changes, so nothing blinks.
 *
 * Keys: arrows choose and change, Enter (or Space) selects, Esc or F12
 * closes, Del restores the default. The mouse points and clicks; the right
 * button closes.
 */
#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "display.h"
#include "shell.h"
#include "audio/audio.h"
#include "usb/usbhid.h"
#include "ui/ui.h"
#include "ff.h"

#define CFG_PATH "/war1/frank.cfg"

extern volatile uint32_t frank_mix_mask;
extern volatile int frank_mouse_div16;
extern volatile bool frank_audio_hdmi;
extern void plat_pause(bool on);
extern void frank_service(void);
extern void frank_quit(void);      /* main.c: a clean restart */

/* ---- Settings ---- */

typedef struct {
    int output;     /* sound output: 1 HDMI, 0 the I2S DAC */
    int volume;     /* 0..10: the DAC's gain, 10 = 128/256 (where it stops
                     * distorting); HDMI twice that, 10 = unity */
    int sound;      /* sound effects on */
    int music;      /* music on */
    int mouse;      /* 1..10 */
} cfg_t;

static const cfg_t defaults = { 1, 10, 1, 1, 5 };
static cfg_t cfg = { 1, 10, 1, 1, 5 };

/* HID counts per screen pixel, in 1/16, by mouse speed */
static const uint8_t mouse_div16[11] = { 0, 64, 48, 40, 32, 24, 20, 16, 13, 11, 9 };

static void apply(void) {
    audio_set_volume((uint32_t)cfg.volume * 128u / 10u);
    frank_mix_mask = (cfg.sound ? 1u : 0u) | (cfg.music ? 2u : 0u);
    frank_mouse_div16 = mouse_div16[cfg.mouse];
    frank_audio_hdmi = cfg.output != 0;
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void settings_load(void) {
    FIL f;
    if (f_open(&f, CFG_PATH, FA_READ) == FR_OK) {
        char line[80];
        while (f_gets(line, sizeof(line), &f)) {
            int v;
            if (!strncmp(line, "output=", 7)) cfg.output = strncmp(line + 7, "i2s", 3) != 0;
            if (sscanf(line, "volume=%d", &v) == 1) cfg.volume = clampi(v, 0, 10);
            if (sscanf(line, "sound=%d", &v) == 1) cfg.sound = v != 0;
            if (sscanf(line, "music=%d", &v) == 1) cfg.music = v != 0;
            if (sscanf(line, "mouse=%d", &v) == 1) cfg.mouse = clampi(v, 1, 10);
            /* older cards: mix=N, bit 0 sound, bit 1 music */
            if (sscanf(line, "mix=%d", &v) == 1) { cfg.sound = v & 1; cfg.music = (v >> 1) & 1; }
        }
        f_close(&f);
    }
    apply();
    printf("settings: output %s, volume %d, sound %s, music %s, mouse %d\n",
           cfg.output ? "hdmi" : "i2s", cfg.volume,
           cfg.sound ? "on" : "off", cfg.music ? "on" : "off", cfg.mouse);
}

static void save(void) {
    char buf[160];
    const int n = snprintf(buf, sizeof(buf),
                           "# frank-war1 settings (F12)\n"
                           "output=%s\nvolume=%d\nsound=%d\nmusic=%d\nmouse=%d\n",
                           cfg.output ? "hdmi" : "i2s", cfg.volume, cfg.sound, cfg.music, cfg.mouse);
    FIL f;
    UINT put = 0;
    if (f_open(&f, CFG_PATH, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        f_write(&f, buf, (UINT)n, &put);
        f_close(&f);
    }
    printf("settings: %s %s\n", CFG_PATH, put == (UINT)n ? "saved" : "NOT saved");
}

/* ---- The items ---- */

enum { K_SLIDER, K_TOGGLE, K_BUTTON };
enum { I_OUTPUT, I_VOLUME, I_SOUND, I_MUSIC, I_MOUSE, I_RESTART, I_RESUME, NITEMS };
#define I_LAST_ROW I_MOUSE

static const struct {
    const char *label;
    int kind, lo, hi;
    const char *off, *on;    /* a toggle's two values */
} items[NITEMS] = {
    [I_OUTPUT]  = { "Sound output", K_TOGGLE, 0, 1, "I2S", "HDMI" },
    [I_VOLUME]  = { "Volume", K_SLIDER, 0, 10 },
    [I_SOUND]   = { "Sound effects", K_TOGGLE, 0, 1, "Off", "On" },
    [I_MUSIC]   = { "Music", K_TOGGLE, 0, 1, "Off", "On" },
    [I_MOUSE]   = { "Mouse speed", K_SLIDER, 1, 10 },
    [I_RESTART] = { "Restart", K_BUTTON, 0, 0 },
    [I_RESUME]  = { NULL, K_BUTTON, 0, 0 },     /* "Return to game" / "Start the game" */
};

static int *value_of(int i) {
    switch (i) {
    case I_OUTPUT: return &cfg.output;
    case I_VOLUME: return &cfg.volume;
    case I_SOUND:  return &cfg.sound;
    case I_MUSIC:  return &cfg.music;
    case I_MOUSE:  return &cfg.mouse;
    }
    return NULL;
}

static int default_of(int i) {
    switch (i) {
    case I_OUTPUT: return defaults.output;
    case I_VOLUME: return defaults.volume;
    case I_SOUND:  return defaults.sound;
    case I_MUSIC:  return defaults.music;
    case I_MOUSE:  return defaults.mouse;
    }
    return 0;
}

/* ---- Layout (320x200) ---- */

#define WX0 52
#define WX1 (UI_W - 52)
#define WY0 18
#define WY1 (UI_H - 18)
#define ROW_Y0 (WY0 + 26)
#define ROW_H 17
#define LABEL_X (WX0 + 16)
#define VAL_X0 (WX1 - 16 - 92)
#define VAL_X1 (WX1 - 16)
#define BTN_Y0 (ROW_Y0 + (I_LAST_ROW + 1) * ROW_H + 6)
#define BTN_Y1 (BTN_Y0 + 16)
#define BTN_W 92
#define BTN_L_X0 (UI_W / 2 - 4 - BTN_W)
#define BTN_R_X0 (UI_W / 2 + 4)
#define HELP_Y (WY1 - 15)

typedef struct { int x0, y0, x1, y1; } box_t;

static box_t item_box(int i) {
    if (i == I_RESTART) return (box_t){ BTN_L_X0, BTN_Y0, BTN_L_X0 + BTN_W, BTN_Y1 };
    if (i == I_RESUME) return (box_t){ BTN_R_X0, BTN_Y0, BTN_R_X0 + BTN_W, BTN_Y1 };
    const int y = ROW_Y0 + i * ROW_H;
    return (box_t){ VAL_X0, y, VAL_X1, y + 14 };
}

/* The row a point is on (label or value), -1 for none. */
static int item_at(int x, int y) {
    for (int i = 0; i < NITEMS; i++) {
        box_t b = item_box(i);
        if (items[i].kind != K_BUTTON) b.x0 = LABEL_X - 4;
        if (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1) return i;
    }
    return -1;
}

/* ---- State ---- */

static volatile bool requested;
static bool open_now, in_game_now;
static int sel, confirm_yes = -1;     /* confirm_yes >= 0: the restart question is up */
static int mx = UI_W / 2, my = UI_H / 2;
static bool mouse_seen;
static int32_t macc_x, macc_y;
static char status[48];

void settings_request(void) {
    requested = true;
}

bool settings_pending(void) {
    return requested && !open_now;
}

bool settings_is_open(void) {
    return open_now;
}

/* ---- Drawing ---- */

static void draw_slider(const box_t *b, int v, int lo, int hi, bool on) {
    const int n = hi - lo + 1, x0 = b->x0 + 5, x1 = b->x1 - 5;
    const int seg = (x1 - x0) / n;
    for (int k = 0; k < n; k++) {
        const int sx = x0 + k * seg;
        const bool lit = lo + k <= v;
        ui_fill(sx, b->y0 + 4, sx + seg - 1, b->y1 - 4,
                lit ? (on ? UI_GOLD : UI_RGB(222, 178, 82)) : UI_RGB(8, 28, 46));
        if (lit) ui_fill(sx, b->y1 - 5, sx + seg - 1, b->y1 - 4, UI_GOLD_DK);
    }
}

static void draw_cursor(void) {
    /* Warcraft's arrow, more or less: a gold arrow outlined in ink */
    static const char *shape[] = {
        "X", "XX", "XoX", "XooX", "XoooX", "XooooX", "XoooooX", "XooooooX",
        "XoooXXXX", "XoXooX", "XX XooX", "X   XooX", "    XoX", "     X",
    };
    for (int r = 0; r < (int)(sizeof shape / sizeof shape[0]); r++)
        for (int k = 0; shape[r][k]; k++) {
            const char c = shape[r][k];
            if (c == ' ') continue;
            const int x = mx + k, y = my + r;
            if (x < UI_W && y < UI_H)
                ui_fb()[y * UI_W + x] = c == 'X' ? UI_INK : (k == 1 || r == k) ? UI_RGB(255, 247, 206) : UI_GOLD;
        }
}

static void draw_button(int i, const char *label) {
    const box_t b = item_box(i);
    const bool on = sel == i;
    ui_box(b.x0, b.y0, b.x1, b.y1, on ? UI_NAVY_HI : UI_NAVY, on);
    ui_text_center(UI_FONT_MENU, (b.x0 + b.x1) / 2, b.y0 + 3, label, on ? UI_GOLD : UI_CREAM, on ? -1 : 0);
}

static void draw_confirm(void) {
    const int x0 = 84, x1 = UI_W - 84, y0 = 70, y1 = 132;
    ui_window(x0, y0, x1, y1);
    ui_text_center(UI_FONT_MENU, UI_W / 2, y0 + 12, "Restart the board?", UI_CREAM, -1);
    for (int k = 0; k < 2; k++) {
        const int bx0 = k ? UI_W / 2 + 6 : UI_W / 2 - 6 - 56, bx1 = bx0 + 56;
        const bool on = confirm_yes == (k == 0);
        ui_box(bx0, y1 - 28, bx1, y1 - 12, on ? UI_NAVY_HI : UI_NAVY, on);
        ui_text_center(UI_FONT_MENU, (bx0 + bx1) / 2, y1 - 25, k ? "No" : "Yes", on ? UI_GOLD : UI_CREAM, on ? -1 : 0);
    }
}

static void draw(int canvas) {
    const int front = display_front_index();
    int t = 0;
    while (t == canvas || t == front) t++;
    ui_target(display_buf565(t));
    ui_dim_copy(display_buf565(canvas));
    ui_window(WX0, WY0, WX1, WY1);
    ui_text_center(UI_FONT_MENU, UI_W / 2, WY0 + 10, "Settings", UI_GOLD, -1);
    for (int i = 0; i <= I_LAST_ROW; i++) {
        const box_t b = item_box(i);
        const bool on = sel == i;
        ui_text(UI_FONT_MENU, LABEL_X, b.y0 + 3, items[i].label, on ? UI_GOLD : UI_CREAM, on ? -1 : 0);
        ui_box(b.x0, b.y0, b.x1, b.y1, on ? UI_NAVY_HI : UI_NAVY, on);
        const int v = *value_of(i);
        if (items[i].kind == K_SLIDER) {
            draw_slider(&b, v, items[i].lo, items[i].hi, on);
        } else {
            ui_text_center(UI_FONT_MENU, (b.x0 + b.x1) / 2, b.y0 + 3, v ? items[i].on : items[i].off,
                           on ? UI_GOLD : UI_CREAM, -1);
        }
    }
    draw_button(I_RESTART, "Restart");
    draw_button(I_RESUME, in_game_now ? "Return to game" : "Start the game");
    ui_text_center(UI_FONT_SMALL, UI_W / 2, HELP_Y, status[0] ? status :
                   "Enter: select    Esc: close", status[0] ? UI_GOLD : UI_DIM, -1);
    if (confirm_yes >= 0) draw_confirm();
    if (mouse_seen) draw_cursor();
    display_show565(t);
}

/* ---- Input ---- */

enum { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_CLOSE, A_DEFAULT, A_YES, A_NO };

static int action_of(uint8_t hid) {
    switch (hid) {
    case 0x52: return A_UP;
    case 0x51: return A_DOWN;
    case 0x50: return A_LEFT;
    case 0x4F: return A_RIGHT;
    case 0x28: case 0x58: case 0x2C: return A_OK;   /* Enter, keypad Enter, Space */
    case 0x29: case 0x2A: return A_BACK;            /* Esc, Backspace */
    case 0x45: return A_CLOSE;                      /* F12 */
    case 0x4C: return A_DEFAULT;                    /* Delete */
    case 0x1C: return A_YES;                        /* Y */
    case 0x11: return A_NO;                         /* N */
    }
    return A_NONE;
}

static bool closing;

static void say(const char *fmt, int v) {
    snprintf(status, sizeof(status), fmt, v);
}

static void changed(int i) {
    apply();
    if (i == I_VOLUME) say("Volume: %d%%", cfg.volume * 10);
    else if (i == I_MOUSE) say("Mouse speed: %d", cfg.mouse);
    else if (i == I_SOUND) say(cfg.sound ? "Sound effects on" : "Sound effects off", 0);
    else if (i == I_MUSIC) say(cfg.music ? "Music on" : "Music off", 0);
    else if (i == I_OUTPUT) say(cfg.output ? "Sound on HDMI" : "Sound on the I2S DAC", 0);
}

static void adjust(int i, int d) {
    int *v = value_of(i);
    if (!v) return;
    const int nv = items[i].kind == K_TOGGLE ? !*v : clampi(*v + d, items[i].lo, items[i].hi);
    if (nv == *v) return;
    *v = nv;
    changed(i);
}

static void restart(void) {
    printf("settings: restart\n");
    frank_quit();
}

static void activate(int i) {
    if (i == I_RESUME) closing = true;
    else if (i == I_RESTART) confirm_yes = 0;
    else adjust(i, +1);
}

static void confirm_action(int a) {
    if (a == A_LEFT || a == A_RIGHT || a == A_UP || a == A_DOWN) confirm_yes = !confirm_yes;
    else if (a == A_YES || (a == A_OK && confirm_yes)) restart();
    else if (a == A_NO || a == A_BACK || a == A_CLOSE || a == A_OK) confirm_yes = -1;
}

static void key_action(int a) {
    status[0] = 0;
    if (confirm_yes >= 0) { confirm_action(a); return; }
    switch (a) {
    case A_UP:
        sel = sel == I_RESTART || sel == I_RESUME ? I_LAST_ROW : sel == 0 ? I_RESUME : sel - 1;
        break;
    case A_DOWN:
        sel = sel == I_LAST_ROW ? I_RESUME : sel >= I_RESTART ? 0 : sel + 1;
        break;
    case A_LEFT:
        if (sel == I_RESUME) sel = I_RESTART;
        else adjust(sel, -1);
        break;
    case A_RIGHT:
        if (sel == I_RESTART) sel = I_RESUME;
        else adjust(sel, +1);
        break;
    case A_OK: activate(sel); break;
    case A_BACK: case A_CLOSE: closing = true; break;
    case A_DEFAULT: {
        int *v = value_of(sel);
        if (v && *v != default_of(sel)) { *v = default_of(sel); changed(sel); }
        break;
    }
    }
}

static int injected[8];
static volatile int inj_w, inj_r;

void settings_key(uint8_t hid) {
    if (inj_w - inj_r < 8) injected[inj_w++ % 8] = hid;
}

/* A click at the mouse: what it points at, acted on. */
static void click(void) {
    status[0] = 0;
    if (confirm_yes >= 0) {
        if (my >= 132 - 28 && my < 132 - 12) {
            if (mx >= UI_W / 2 - 62 && mx < UI_W / 2 - 6) restart();
            if (mx >= UI_W / 2 + 6 && mx < UI_W / 2 + 62) confirm_yes = -1;
        }
        return;
    }
    const int i = item_at(mx, my);
    if (i < 0) return;
    sel = i;
    const box_t b = item_box(i);
    if (items[i].kind == K_SLIDER && mx >= b.x0) {
        /* the segment clicked */
        const int n = items[i].hi - items[i].lo + 1, seg = (b.x1 - b.x0 - 10) / n;
        const int v = clampi(items[i].lo + (mx - b.x0 - 5) / seg, items[i].lo, items[i].hi);
        int *pv = value_of(i);
        if (v != *pv) { *pv = v; changed(i); }
    } else {
        activate(i);
    }
}

/* ---- The loop ---- */

void settings_run(bool in_game) {
    if (open_now) return;
    requested = false;
    open_now = true;
    in_game_now = in_game;
    closing = false;
    confirm_yes = -1;
    status[0] = 0;
    sel = I_RESUME;
    const cfg_t before = cfg;
    printf("settings: open\n");
    if (in_game) plat_pause(true);

    /* the picture behind: the game's frame on screen, or the shell's own */
    int canvas = in_game ? -1 : shell_work_frame();
    if (canvas < 0) canvas = display_front_index();

    /* keys already held (F12 itself) must not act in the window */
    uint8_t code;
    int down;
    usbhid_task();
    while (usbhid_get_key_action(&code, &down)) {}
    usbhid_mouse_state_t m;
    usbhid_get_mouse_state(&m);
    uint8_t buttons_prev = m.buttons;

    bool dirty = true;
    uint64_t last_draw = 0;
    while (!closing) {
        if (dirty && time_us_64() - last_draw >= 16000) {
            draw(canvas);
            dirty = false;
            last_draw = time_us_64();
        }
        usbhid_task();
        frank_service();       /* the console stays alive */
        while (usbhid_get_key_action(&code, &down))
            if (down) { key_action(action_of(code)); dirty = true; }
        while (inj_r != inj_w) {
            key_action(action_of((uint8_t)injected[inj_r++ % 8]));
            dirty = true;
        }
        usbhid_get_mouse_state(&m);
        if (m.dx || m.dy) {
            macc_x += m.dx * 16;
            macc_y -= m.dy * 16;
            const int div = frank_mouse_div16;
            mx = clampi(mx + macc_x / div, 0, UI_W - 1);
            my = clampi(my + macc_y / div, 0, UI_H - 1);
            macc_x %= div;
            macc_y %= div;
            mouse_seen = true;
            if (confirm_yes < 0) {
                const int i = item_at(mx, my);
                if (i >= 0) sel = i;
            }
            dirty = true;
        }
        const uint8_t pressed = m.buttons & ~buttons_prev;
        buttons_prev = m.buttons;
        if (pressed & 1) { mouse_seen = true; click(); dirty = true; }
        if (pressed & 2) { if (confirm_yes >= 0) confirm_yes = -1; else closing = true; dirty = true; }
        sleep_us(500);
    }

    if (memcmp(&before, &cfg, sizeof cfg)) save();
    /* the mouse button that closed the window must not reach the game */
    for (uint64_t until = time_us_64() + 2000000; m.buttons && time_us_64() < until;) {
        usbhid_task();
        usbhid_get_mouse_state(&m);
    }
    while (usbhid_get_key_action(&code, &down)) {}
    if (in_game) {
        ui_release();
        display_show565(canvas);   /* the game's picture until its next frame */
        plat_pause(false);
    } else {
        shell_refresh();
    }
    open_now = false;
    printf("settings: closed\n");
}
