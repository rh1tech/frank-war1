/*
 * frank-war1 — plat.h on the board: HDMI (display.c, RGB565), USB HID,
 * the I2S DAC and OPL2 music on core 1, time from the microsecond timer.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Audio runs in two halves. war1 mixes its sounds in an SDL-style callback
 * that walks the game's entity lists; upstream calls it from SDL's audio
 * thread, racing the game. Here it is called on core 0 between frames
 * (plat_present, once per frame) for as many 11025 Hz samples as the time
 * since the last call needs, into a ring. Core 1 (plat_audio_service, in
 * its idle loop) takes them from the ring, upsamples them to the OPL's
 * 49716 Hz, adds the music and feeds the DAC. war1's MIDI events (tsf calls,
 * port/tsf_frank.h) are queued with their position in the 11025 Hz stream
 * and played when core 1 gets there, so music stays in step with the sound.
 */
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "display.h"
#include "audio/audio.h"
#include "usb/usbhid.h"
#include "frank_game.h"
#include "opl_war.h"
#include "shell.h"
#include "ff.h"

/* ----------------------------------------------------------------- replay */

static int cur_x = PLAT_W / 2, cur_y = PLAT_H / 2;
static void audio_generate(void);

/* A recording from the host (host/plat_host.c, WAR_RECORD): input events
 * at the frames they happened, and the game's state hash every second.
 * With the game on the fixed 1/60 s step and the sound generated per frame,
 * the board plays the same game; the hashes show it does. */
extern bool war_fixedStep;
extern uint32_t war_stateHash(void *context);
extern void *war_context;
static FIL replay_file;
static bool replaying;
static uint32_t replay_frames;          /* frames shown since the replay began */
static char replay_line[96];
static long replay_line_frame = -1;
static uint32_t replay_hashes, replay_bad, replay_first_bad;
static uint64_t replay_t0;

bool plat_replay_start(const char *path) {
    if (f_open(&replay_file, path, FA_READ) != FR_OK) {
        printf("replay: cannot open %s\n", path);
        return false;
    }
    replaying = true;
    war_fixedStep = true;
    replay_t0 = time_us_64();
    printf("replay: %s\n", path);
    return true;
}

bool plat_replaying(void) {
    return replaying;
}

static void replay_end(void) {
    const uint64_t us = time_us_64() - replay_t0;
    f_close(&replay_file);
    replaying = false;
    printf("replay: done, %lu frames in %lu.%01lu s = %lu.%01lu fps; state hashes %lu/%lu matched",
           (unsigned long)replay_frames, (unsigned long)(us / 1000000u), (unsigned long)(us / 100000u % 10u),
           (unsigned long)(us ? (uint64_t)replay_frames * 1000000u / us : 0),
           (unsigned long)(us ? (uint64_t)replay_frames * 10000000u / us % 10u : 0),
           (unsigned long)(replay_hashes - replay_bad), (unsigned long)replay_hashes);
    if (replay_bad) printf(", first difference at frame %lu", (unsigned long)replay_first_bad);
    printf("\n");
}

static void replay_frame(void) {
    if (!replaying) return;
    replay_frames++;
    for (;;) {
        if (replay_line_frame < 0) {
            if (!f_gets(replay_line, sizeof(replay_line), &replay_file)) { replay_end(); return; }
            if (replay_line[0] == '#' || replay_line[0] == '\n') continue;
            replay_line_frame = strtol(replay_line, NULL, 10);
        }
        if ((long)replay_frames < replay_line_frame) return;
        char cmd[8] = "", b = 'L', st[8] = "";
        int x = 0, y = 0;
        unsigned u1 = 0, u2 = 0;
        sscanf(replay_line, "%*ld %7s", cmd);
        const char *args = strchr(strchr(replay_line, ' ') + 1, ' ');
        args = args ? args + 1 : "";
        if (!strcmp(cmd, "move") && sscanf(args, "%d %d", &x, &y) == 2) {
            cur_x = x; cur_y = y;
            fsdl_mouse_move(x, y);
        } else if ((!strcmp(cmd, "down") || !strcmp(cmd, "up")) && sscanf(args, " %c %d %d", &b, &x, &y) == 3) {
            cur_x = x; cur_y = y;
            fsdl_mouse_button(b == 'R' ? 3 : b == 'M' ? 2 : 1, cmd[0] == 'd', x, y);
        } else if (!strcmp(cmd, "keym") && sscanf(args, "%u %u %7s", &u1, &u2, st) == 3) {
            fsdl_key((uint8_t)u1, (uint8_t)u2, !strcmp(st, "down"));
        } else if (!strcmp(cmd, "char")) {
            fsdl_text((char)atoi(args));
        } else if (!strcmp(cmd, "hash")) {
            const uint32_t want = (uint32_t)strtoul(args, NULL, 16), got = war_stateHash(war_context);
            replay_hashes++;
            if (got != want) {
                if (!replay_bad) {
                    replay_first_bad = replay_frames;
                    printf("replay: frame %lu state %08lx, recorded %08lx: diverged\n",
                           (unsigned long)replay_frames, (unsigned long)got, (unsigned long)want);
                }
                replay_bad++;
            }
        } else if (!strcmp(cmd, "quit")) {
            replay_end();
            return;
        }
        replay_line_frame = -1;
    }
}

/* ------------------------------------------------------------------ video */

static uint64_t t_start;

bool plat_init(void) {
    t_start = time_us_64();
    /* The game's cursor sits at 0,0 until the mouse first moves: on the
     * screen's corner, where it edge-scrolls the map to its corner (all fog)
     * when no mouse is plugged in or it has not moved yet. Start it where
     * the board's cursor is, mid-screen. */
    fsdl_mouse_move(cur_x, cur_y);
    return true;
}

uint16_t *plat_backbuffer(void) {
    return display_draw565();
}

uint16_t *plat_present(void) {
    if (shell_active) {
        /* The game's first frame: drop the boot shell for good. */
        shell_release(NULL);
        display_set_rgb565(true);
    }
    /* Show it from the next output frame; the game draws on into the third
     * buffer (it waits only when two frames ahead), so it never draws into
     * the frame on screen (no tearing) nor stalls to the vsync. */
    display_present(false);
    frank_frame_shown();
    /* the game's sound for this frame: here, at the same point in the frame
     * as on the host (the callback ends sounds, which the game sees) */
    audio_generate();
    replay_frame();
    return display_draw565();
}

/* ------------------------------------------------------------------- time */

uint64_t plat_time_ns(void) {
    return (time_us_64() - t_start) * 1000ull;
}

void plat_sleep_ms(uint32_t ms) {
    if (ms) sleep_ms(ms);
    else tight_loop_contents();
}

/* ------------------------------------------------------------------ input */

static uint8_t buttons_prev;
static uint8_t mods;
/* Mouse speed: HID counts per screen pixel, in 1/16. */
#define MOUSE_DIV16 24
static int32_t acc_x, acc_y;

void plat_poll(void) {
    usbhid_task();
    frank_service();
    if (replaying) {
        /* the recording is the input; USB is read and dropped */
        uint8_t code;
        int down;
        while (usbhid_get_key_action(&code, &down)) {}
        usbhid_mouse_state_t m;
        usbhid_get_mouse_state(&m);
        return;
    }

    uint8_t code;
    int down;
    while (usbhid_get_key_action(&code, &down)) {
        if (code >= 0xE0 && code <= 0xE7) {
            const uint8_t bit = (uint8_t)(1u << (code - 0xE0));
            mods = down ? (mods | bit) : (mods & ~bit);
        }
        fsdl_key(code, mods, down != 0);
        if (down) {
            const uint8_t ch = usbhid_keycode_to_ascii(code, mods);
            if (ch >= 32 && ch < 127) fsdl_text((char)ch);
        }
    }

    usbhid_mouse_state_t m;
    usbhid_get_mouse_state(&m);
    if (m.dx || m.dy) {
        /* usbhid reports up as positive dy; the screen's y grows down */
        acc_x += m.dx * 16;
        acc_y -= m.dy * 16;
        const int nx = cur_x + acc_x / MOUSE_DIV16, ny = cur_y + acc_y / MOUSE_DIV16;
        acc_x %= MOUSE_DIV16;
        acc_y %= MOUSE_DIV16;
        cur_x = nx < 0 ? 0 : nx >= PLAT_W ? PLAT_W - 1 : nx;
        cur_y = ny < 0 ? 0 : ny >= PLAT_H ? PLAT_H - 1 : ny;
        fsdl_mouse_move(cur_x, cur_y);
    }
    const uint8_t changed = m.buttons ^ buttons_prev;
    if (changed & 1) fsdl_mouse_button(1, (m.buttons & 1) != 0, cur_x, cur_y);
    if (changed & 2) fsdl_mouse_button(3, (m.buttons & 2) != 0, cur_x, cur_y);
    if (changed & 4) fsdl_mouse_button(2, (m.buttons & 4) != 0, cur_x, cur_y);
    buttons_prev = m.buttons;
}

void plat_set_cursor(int x, int y) {
    cur_x = x;
    cur_y = y;
}

/* ------------------------------------------------------------------ audio */

/* Ring of the game's 11025 Hz mono samples, core 0 -> core 1. */
#define RING 4096u   /* ~370 ms */
static int16_t ring[RING];
static volatile uint32_t ring_w, ring_r;   /* sample counts, wrap freely */
static int game_rate;
static uint64_t gen_t0;
static uint64_t gen_done;
static bool audio_on;

/* MIDI events, core 0 -> core 1, stamped with their ring position. */
#define MIDIQ 512u
static struct { uint32_t pos; uint8_t st, a, b; } midiq[MIDIQ];
static volatile uint32_t midi_w, midi_r;
static uint32_t midi_pos;     /* core 0: position of the events being queued */

void plat_midi_event(uint8_t status, uint8_t a, uint8_t b) {
    if (midi_w - midi_r >= MIDIQ) return;
    const uint32_t i = midi_w % MIDIQ;
    midiq[i].pos = midi_pos;
    midiq[i].st = status;
    midiq[i].a = a;
    midiq[i].b = b;
    __dmb();
    midi_w++;
}

void plat_midi_advance(int samples) {
    midi_pos += (uint32_t)samples;
}

void plat_midi_reset(void) {
    plat_midi_event(0xFF, 0, 0);   /* all notes off, on core 1 */
}

bool plat_audio_open(int freq) {
    game_rate = freq;
    gen_t0 = time_us_64();
    gen_done = 0;
    audio_on = true;
    return true;
}

void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}

/* Core 0, once a frame: the game's sound up to now (plus a little lead),
 * as much as the ring holds. On the fixed step (replays) the amount is the
 * frame count's, exactly as on the host, and the callback runs for all of
 * it even when the ring is full (it ends sounds: the game depends on it). */
static uint32_t frames_generated;

static void audio_generate(void) {
    if (!audio_on) return;
    uint64_t due;
    if (war_fixedStep) {
        frames_generated++;
        due = (uint64_t)frames_generated * (uint64_t)game_rate / 60u;
    } else {
        due = (time_us_64() - gen_t0) * (uint64_t)game_rate / 1000000u + (uint64_t)game_rate / 15u;
    }
    static int16_t scratch[512];
    while (gen_done < due) {
        const uint32_t free = RING - (ring_w - ring_r);
        uint32_t n = (uint32_t)(due - gen_done);
        if (n > free) n = free;
        const uint32_t w = ring_w % RING;
        if (n > RING - w) n = RING - w;
        if (!n) {
            if (!war_fixedStep) {
                /* Ring full (core 1 behind): drop the debt rather than burst later. */
                gen_done = due;
                break;
            }
            n = (uint32_t)(due - gen_done);
            if (n > 512) n = 512;
            fsdl_audio_render(scratch, (int)n);   /* heard by nobody */
            gen_done += n;
            continue;
        }
        midi_pos = ring_w;
        fsdl_audio_render(&ring[w], (int)n);
        __dmb();
        ring_w += n;
        gen_done += n;
    }
}

/* Core 1. */
static uint32_t phase_q16;        /* position between two game samples */
static uint32_t step_q16;         /* game samples per output sample */
static int16_t last_sample;
volatile uint32_t frank_mix_mask = 3;

static void midi_apply(uint32_t upto) {
    while (midi_r != midi_w) {
        const uint32_t i = midi_r % MIDIQ;
        if ((int32_t)(midiq[i].pos - upto) > 0) break;
        if (midiq[i].st == 0xFF) opl_war_reset();
        else opl_war_midi(midiq[i].st, midiq[i].a, midiq[i].b);
        __dmb();
        midi_r++;
    }
}

bool plat_audio_service(void) {
    extern void frank_hang_check(void);
    frank_hang_check();
    if (!game_rate) return false;
    if (!step_q16) step_q16 = (uint32_t)(((uint64_t)game_rate << 16) / OPL_WAR_RATE);

    static int16_t out[256 * 2];
    bool did = false;
    while (audio_space() >= sizeof(out)) {
        const uint32_t frames = sizeof(out) / 4;
        for (uint32_t i = 0; i < frames; i++) {
            /* linear interpolation between the last and the next game sample */
            int32_t s = 0;
            if (ring_r != ring_w) {
                const int32_t a = last_sample, b = ring[ring_r % RING];
                s = a + (((b - a) * (int32_t)(phase_q16 >> 1)) >> 15);
                phase_q16 += step_q16;
                while (phase_q16 >= 65536u && ring_r != ring_w) {
                    phase_q16 -= 65536u;
                    last_sample = ring[ring_r % RING];
                    ring_r++;
                }
            }
            if (!(frank_mix_mask & 1)) s = 0;
            out[2 * i] = out[2 * i + 1] = (int16_t)s;
        }
        midi_apply(ring_r);
        if (frank_mix_mask & 2) opl_war_mix(out, frames);
        audio_write(out, sizeof(out));
        did = true;
    }
    return did;
}

/* ------------------------------------------------------------------ locks */

/* The game runs on core 0 only (audio included, see above): no contention. */
static int dummy_mutex;
void *plat_mutex_create(void) { return &dummy_mutex; }
void plat_mutex_lock(void *m) { (void)m; }
void plat_mutex_unlock(void *m) { (void)m; }
void plat_mutex_destroy(void *m) { (void)m; }

/* -------------------------------------------------------------------- log */

void plat_log(int priority, const char *line) {
    static const char tag[] = "?TVDIWEC";
    printf("[%c] %s\n", priority >= 0 && priority < 8 ? tag[priority] : '?', line);
    if (priority >= 6) shell_puts_quiet(line);
}

void plat_quit(void) {
    frank_quit();
}
