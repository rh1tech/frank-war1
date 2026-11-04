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

void opl_war_mix(int16_t *pcm, uint32_t frames) {
    if (!opl) return;
    static int16_t buf[256];
    while (frames) {
        uint32_t n = frames > 256 ? 256 : frames;
        OPL_calc_buffer(opl, buf, n);
        for (uint32_t i = 0; i < n; i++) {
            const int32_t s = buf[i];
            const int32_t l = pcm[0] + s, r = pcm[1] + s;
            pcm[0] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            pcm[1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            pcm += 2;
        }
        frames -= n;
    }
}
