// frank-war1 — the original's unit animation scripts as war1's actions.
//
// WarCraft 1.20 animates (and times) every unit with small scripts it loads
// from DATA.WAR (war1's entry 212): a table per unit type of six animations
// (death, -, idle, walk, attack/work, train), each a list of byte opcodes run
// once a game tick by the unit update (WAR.EXE 0x10473):
//   0       checkpoint: wait 1 tick (the order may change here)
//   1 n     wait n ticks            7 n  wait n - the owner's speed upgrade
//   10 n    wait n ticks (+1 when slowed)
//   2       stop                    3 w  jump to w
//   4 f     frame f                 5 d  move d pixels, 6 d f  both
//   8       sound                   9    the action: a blow, a chop, ...
// Frames are numbered as war1 numbers them (a base, plus the direction).
// At start-up the attack and walk actions of every unit type are rebuilt
// from these scripts, their waits in original ticks (30 ms at normal speed),
// and walking speeds are worked out from the pixels the walk script moves.
// Read from the player's DATA.WAR: no game data in this source.
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib

#define WSCR_RESOURCE 212
#define WSCR_TICK_SECONDS 0.03f
#define WSCR_TYPES 32

static f32 wscr_speeds[WSCR_TYPES][3];   // walking speed, px/s, by upgrade level (0: unknown)

static const u8* wscr_data;
static u32 wscr_length;

static u16 wscr_u16(u32 o)
{
    return o + 1 < wscr_length ? (u16)(wscr_data[o] | wscr_data[o + 1] << 8) : 0;
}

static u32 wscr_animation(s32 type, s32 anim)
{
    u32 base = wscr_u16((u32)type * 2) / 2u;
    return wscr_u16((base + (u32)anim) * 2u);
}

// war1's action from the script at `start`: frames, waits (in ticks), the
// action step, the sound; loops when the script jumps back.
static bool wscr_buildAction(WarUnitActionDef* def, u32 start, WarUnitActionStepType sound)
{
    WarUnitActionDef out = createUnitActionDef(def->type, def->directional, false);
    out.frankTicks = true;
    u32 p = start;
    for (s32 n = 0; n < 200 && p < wscr_length; n++)
    {
        u8 op = wscr_data[p];
        switch (op)
        {
            case 0: addUnitActionDefStep(&out, WAR_ACTION_STEP_WAIT, 1); p += 1; break;
            case 1: case 7: case 10:
                addUnitActionDefStep(&out, WAR_ACTION_STEP_WAIT, wscr_data[p + 1] ? wscr_data[p + 1] : 256);
                p += 2;
                break;
            case 2: goto done;
            case 3:
                out.loop = true;   // (the scripts jump back to their start)
                goto done;
            case 4: addUnitActionDefStep(&out, WAR_ACTION_STEP_FRAME, wscr_data[p + 1]); p += 2; break;
            case 5: p += 2; break;
            case 6: addUnitActionDefStep(&out, WAR_ACTION_STEP_FRAME, wscr_data[p + 2]); p += 3; break;
            case 8: if (sound != WAR_ACTION_STEP_NONE) addUnitActionDefStep(&out, sound, 0); p += 1; break;
            case 9: addUnitActionDefStep(&out, WAR_ACTION_STEP_ATTACK, 0); p += 1; break;
            default: WarUnitActionStepListFree(&out.steps); return false;
        }
    }
done:
    // war1 needs a wait to end each pass
    if (!out.steps.count || out.steps.items[out.steps.count - 1].type != WAR_ACTION_STEP_WAIT)
        addUnitActionDefStep(&out, WAR_ACTION_STEP_WAIT, 1);
    // (war1's old step list is shared by paired types, footman and grunt:
    // left as it is)
    *def = out;
    return true;
}

// Pixels per second the walk script moves at an upgrade level (its op 7
// waits shorten by the level), over a few loops.
static f32 wscr_walkSpeed(u32 start, s32 level)
{
    s32 pixels = 0, ticks = 0, loops = 0;
    u32 p = start;
    for (s32 n = 0; n < 2000 && p < wscr_length && loops < 4; n++)
    {
        u8 op = wscr_data[p];
        switch (op)
        {
            case 0: ticks += 1; p += 1; break;
            case 1: case 10: ticks += wscr_data[p + 1]; p += 2; break;
            case 7: ticks += MAX((s32)wscr_data[p + 1] - level, 0); p += 2; break;
            case 2: n = 2000; break;
            case 3: p = wscr_u16(p + 1); loops++; break;
            case 4: p += 2; break;
            case 5: pixels += ABS((s8)wscr_data[p + 1]); p += 2; break;
            case 6: pixels += ABS((s8)wscr_data[p + 1]); p += 3; break;
            case 8: case 9: p += 1; break;
            default: n = 2000; break;
        }
    }
    return ticks > 0 && pixels > 0 ? (f32)pixels / ((f32)ticks * WSCR_TICK_SECONDS) : 0;
}

static WarUnitActionStepType wscr_soundOf(const WarUnitActionDef* def)
{
    for (s32 i = 0; i < def->steps.count; i++)
        if (def->steps.items[i].type >= WAR_ACTION_STEP_SOUND_SWORD) return def->steps.items[i].type;
    return WAR_ACTION_STEP_NONE;
}

void wscr_applyOriginalScripts(WarContext* context)
{
    if (!context->warFile) return;
    WarRawResource* raw = &context->warFile->resources[WSCR_RESOURCE];
    if (!raw->data && !wfile_readResource(context->warFile, WSCR_RESOURCE))
    {
        logWarning("original unit scripts: DATA.WAR entry %d not readable", WSCR_RESOURCE);
        return;
    }
    wscr_data = raw->data;
    wscr_length = raw->length;
    if (!wscr_data || wscr_length < 2 * WSCR_TYPES) return;

    s32 attacks = 0, walks = 0;
    for (s32 t = 0; t < WSCR_TYPES; t++)
    {
        WarUnitActionDef* attack = &gUnitActionDefs[t][WAR_ACTION_TYPE_ATTACK];
        const s32 o = wu_toOriginalType((WarUnitType)t);   // the scripts go by the original's numbers
        if (attack->type == WAR_ACTION_TYPE_ATTACK && wscr_buildAction(attack, wscr_animation(o, 4), wscr_soundOf(attack)))
            attacks++;
        WarUnitActionDef* walk = &gUnitActionDefs[t][WAR_ACTION_TYPE_WALK];
        u32 w = wscr_animation(o, 3);
        if (walk->type == WAR_ACTION_TYPE_WALK && wscr_buildAction(walk, w, WAR_ACTION_STEP_NONE))
        {
            walk->loop = true;
            walks++;
        }
        for (s32 l = 0; l < 3; l++) wscr_speeds[t][l] = wscr_walkSpeed(w, l);
    }
    logInfo("original unit scripts: %d attacks, %d walks", attacks, walks);
}

// The original's walking speed for a unit type and upgrade level, px/s (0:
// none, use war1's).
f32 wscr_unitSpeed(WarUnitType type, s32 level)
{
    if ((u32)type >= WSCR_TYPES || level < 0 || level > 2) return 0;
    return wscr_speeds[type][level];
}
