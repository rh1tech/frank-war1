// frank-war1 — the original's random numbers (WAR.EXE 0x19478): a linear
// congruential generator, seed * 0x15A4E35 + 1, bits 16-30. The game draws
// from it for its level's random unit groups, each new unit's start delay,
// idle turning, misses and more; drawing the same numbers in the same order
// gives the original's game (docs/ai.md "Random numbers").
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib
#pragma once

static u32 wrng_seed = 1;
static bool wrng_levelLoading;   // the level's own units are being made

static u32 wrng_tick;   // the AI's tick (research traces)

static inline s32 wrng_draw(const char* who)
{
#ifndef FRANK_BOARD
    static s32 trace = -1;
    if (trace < 0) trace = getenv("WAR_RNGTRACE") != NULL;
    if (trace) fprintf(stderr, "draw tick %u %s\n", wrng_tick, who);
#else
    (void)who;
#endif
    wrng_seed = wrng_seed * 0x15A4E35u + 1u;
    return (s32)((wrng_seed >> 16) & 0x7FFF);
}
#define wrng_next() wrng_draw(__func__)

// A level begins: the original seeds from the clock; the host takes
// WAR_RNGSEED (hex) to play the same game as a reference run.
static inline void wrng_levelSeed(u32 fallback)
{
#ifndef FRANK_BOARD
    const char* s = getenv("WAR_RNGSEED");
    if (s) { wrng_seed = (u32)strtoul(s, NULL, 16); return; }
#endif
    wrng_seed = fallback;
}
