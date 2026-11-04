// frank-war1 — the TinySoundFont calls war_audio.c makes, without a
// SoundFont. war1 drives tsf with MIDI events from its audio callback and
// asks it to render; here the events go to the platform's music synth
// (OPL2 on the board) through plat_midi_*(), which mixes music itself;
// tsf_render_short() only moves the music's clock on.
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib
#pragma once

#include <stdint.h>

#define TSF_TRUE 1
#define TSF_FALSE 0
#define TSF_RENDER_EFFECTSAMPLEBLOCK 64

enum TSFOutputMode { TSF_STEREO_INTERLEAVED, TSF_STEREO_UNWEAVED, TSF_MONO };

typedef struct tsf { int open; } tsf;

// tml.h reads MIDI through tsf's stream type
struct tsf_stream { void* data; int (*read)(void* data, void* ptr, unsigned int size); int (*skip)(void* data, unsigned int count); };

void plat_midi_event(uint8_t status, uint8_t a, uint8_t b);
void plat_midi_advance(int samples);   // the events so far happen before these samples
void plat_midi_reset(void);

static tsf tsf_frank_instance;

static inline tsf* tsf_load_filename(const char* filename)
{
    (void)filename;
    tsf_frank_instance.open = 1;
    return &tsf_frank_instance;
}

static inline void tsf_close(tsf* f) { if (f) f->open = 0; plat_midi_reset(); }
static inline void tsf_set_output(tsf* f, enum TSFOutputMode mode, int samplerate, float gain_db)
{
    (void)f; (void)mode; (void)samplerate; (void)gain_db;
}
static inline int tsf_channel_set_bank_preset(tsf* f, int channel, int bank, int preset)
{
    (void)f; (void)channel; (void)bank; (void)preset;
    return 1;
}
static inline int tsf_channel_set_presetnumber(tsf* f, int channel, int preset, int drums)
{
    (void)f; (void)drums;
    plat_midi_event((uint8_t)(0xC0 | (channel & 15)), (uint8_t)preset, 0);
    return 1;
}
static inline int tsf_channel_note_on(tsf* f, int channel, int key, float vel)
{
    (void)f;
    int v = (int)(vel * 127.0f + 0.5f);
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    plat_midi_event((uint8_t)(0x90 | (channel & 15)), (uint8_t)key, (uint8_t)v);
    return 1;
}
static inline void tsf_channel_note_off(tsf* f, int channel, int key)
{
    (void)f;
    plat_midi_event((uint8_t)(0x80 | (channel & 15)), (uint8_t)key, 0);
}
static inline int tsf_channel_set_pitchwheel(tsf* f, int channel, int pitch_wheel)
{
    (void)f;
    plat_midi_event((uint8_t)(0xE0 | (channel & 15)), (uint8_t)(pitch_wheel & 0x7F), (uint8_t)((pitch_wheel >> 7) & 0x7F));
    return 1;
}
static inline int tsf_channel_midi_control(tsf* f, int channel, int controller, int value)
{
    (void)f;
    plat_midi_event((uint8_t)(0xB0 | (channel & 15)), (uint8_t)controller, (uint8_t)value);
    return 1;
}
static inline void tsf_render_short(tsf* f, short* buffer, int samples, int flag_mixing)
{
    (void)f; (void)buffer; (void)flag_mixing;
    plat_midi_advance(samples);
}
static inline void tsf_note_off_all(tsf* f, int immediate)
{
    (void)f; (void)immediate;
    plat_midi_reset();
}
