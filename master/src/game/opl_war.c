/*
 * frank-war1 — war1's MIDI played on an emulated OPL2 (emu8950) through
 * Apogee's AL_MIDI voice allocator and General MIDI timbre bank, close to
 * the DOS game on an Adlib. All on core 1: plat_frank.c hands over the
 * events in step with the sound.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "opl_war.h"

#include "emu8950.h"
#include "al_midi.h"
#include "sndcards.h"

#define OPL_CLOCK 3579545u

static OPL *opl;
static uint32_t opl_latch;
volatile uint32_t opl_war_writes, opl_war_events;

/* AL_MIDI writes an Adlib's index port, then its data port. */
void Adlib_Write(int port, int value) {
    if (!(port & 1)) opl_latch = (uint32_t)value & 0xffu;
    else if (opl) { OPL_writeReg(opl, opl_latch, (uint8_t)value); opl_war_writes++; }
}

void opl_war_init(void) {
    if (!opl) opl = OPL_new(OPL_CLOCK, OPL_WAR_RATE);
    if (!opl) return;
    OPL_reset(opl);
    AL_Init(Adlib);
}

void opl_war_reset(void) {
    if (!opl) return;
    for (int ch = 0; ch < 16; ch++) AL_AllNotesOff(ch);
}

void opl_war_midi(uint8_t status, uint8_t a, uint8_t b) {
    if (!opl) return;
    opl_war_events++;
    const int ch = status & 15;
    switch (status & 0xF0) {
    case 0x80: AL_NoteOff(ch, a, b); break;
    case 0x90: if (b) AL_NoteOn(ch, a, b); else AL_NoteOff(ch, a, 0); break;
    case 0xB0: AL_ControlChange(ch, a, b); break;
    case 0xC0: AL_ProgramChange(ch, a); break;
    case 0xE0: AL_SetPitchBend(ch, a, b); break;
    default: break;
    }
}

/* The emulated OPL2's output peaks near 2300 of 32767, some 22 dB under the
 * game's sound effects (they reach 29500): mixed 4x (+12 dB), as loud as
 * the music sits under the effects on the original's sound cards. */
#define OPL_WAR_MUSIC_GAIN 4

/* The sum of a loud sound effect and the music can pass full scale: above
 * 3/4 of it, bent smoothly towards full scale instead of cut off. */
#define KNEE 24576
static inline int16_t soft_clip(int32_t v) {
    const int32_t a = v < 0 ? -v : v;
    if (a <= KNEE) return (int16_t)v;
    const int32_t room = 32767 - KNEE, over = a - KNEE;
    const int32_t y = KNEE + over * room / (room + over);
    return (int16_t)(v < 0 ? -y : y);
}

/* The music's loudest sample since the meter was last read (main.c `snd`). */
volatile uint32_t opl_war_peak;

void opl_war_mix(int16_t *pcm, uint32_t frames) {
    if (!opl) return;
    static int16_t buf[256];
    while (frames) {
        uint32_t n = frames > 256 ? 256 : frames;
        OPL_calc_buffer(opl, buf, n);
        for (uint32_t i = 0; i < n; i++) {
            const int32_t s = buf[i] * OPL_WAR_MUSIC_GAIN;
            const uint32_t m = (uint32_t)(s < 0 ? -s : s);
            if (m > opl_war_peak) opl_war_peak = m;
            pcm[0] = soft_clip(pcm[0] + s);
            pcm[1] = soft_clip(pcm[1] + s);
            pcm += 2;
        }
        frames -= n;
    }
}
