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
#include "war_orders_frank.c"
#define WSCR_TICK_SECONDS 0.03f
#define WSCR_TYPES 32

extern bool wai_isComputerPlayer(WarContext* context, s32 player);

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
        // a worker chops with its attack script (its sixth animation's work)
        WarUnitActionDef* harvest = &gUnitActionDefs[t][WAR_ACTION_TYPE_HARVEST];
        if (harvest->type == WAR_ACTION_TYPE_HARVEST && wscr_buildAction(harvest, wscr_animation(o, 4), wscr_soundOf(harvest)))
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

// The original's walk script of a unit type (port/war_move_frank.inc runs
// it): false when DATA.WAR's scripts weren't read.
bool wscr_walkScript(WarUnitType type, const u8** data, u32* length, u32* start)
{
    if (!wscr_data || (u32)type >= WSCR_TYPES) return false;
    u32 w = wscr_animation(wu_toOriginalType(type), 3);
    if (w >= wscr_length) return false;
    *data = wscr_data;
    *length = wscr_length;
    *start = w;
    return true;
}

// The original's attack (work) script of a unit type.
bool wscr_attackScript(WarUnitType type, const u8** data, u32* length, u32* start)
{
    if (!wscr_data || (u32)type >= WSCR_TYPES) return false;
    u32 a = wscr_animation(wu_toOriginalType(type), 4);
    if (a >= wscr_length) return false;
    *data = wscr_data;
    *length = wscr_length;
    *start = a;
    return true;
}

typedef struct
{
    WarContext* context;
    WarEntity* entity;
    void (*action)(WarContext*, WarEntity*);
} WarOriginalCall;

static void wos_error(WarOriginalRuntime* runtime, uint16_t slot, const char* message)
{
    WarOriginalCall* call = runtime->context;
    logError("original kernel: entity %u slot %u: %s", call->entity ? call->entity->id : 0, slot, message);
}

static uint16_t wos_random(WarOriginalRuntime* runtime)
{
    NOT_USED(runtime);
    return (uint16_t)wrng_next();
}

static void wos_publish(WarOriginalRuntime* runtime, uint16_t slot)
{
    WarOriginalCall* call = runtime->context;
    WarOriginalUnit* original = &runtime->units[slot];
    WarUnitComponent* unit = we_getUnitComponent(call->context, call->entity);
    wu_setUnitCenterPosition(call->context, call->entity, vec2i(original->pixelX, original->pixelY));
    if (unit->invisible && !original->invisibility)
    {
        unit->invisible = false;
        unit->invisibilityTime = 0;
        wai_targetsDirty = true;
    }
}

static WarOrderResult wos_action(WarOriginalRuntime* runtime, uint16_t slot)
{
    WarOriginalCall* call = runtime->context;
    if (!call->action)
    {
        wos_error(runtime, slot, "missing game action handler");
        return WOR_ERROR;
    }
    wos_publish(runtime, slot);
    call->action(call->context, call->entity);
    return WOR_UNCHANGED;
}

static WarOriginalRuntime wos_runtime(WarOriginalCall* call)
{
    return (WarOriginalRuntime){
        .units = we_getEntityManager(call->context)->originalUnits,
        .scripts = wscr_data, .scriptLength = wscr_length, .context = call,
        .random = wos_random, .action = wos_action, .error = wos_error
    };
}

WarOriginalUnit* wos_unit(WarContext* context, WarEntity* entity)
{
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    if (!unit || wu_isCorpseUnitType(unit->type)) return NULL;
    WarEntityManager* manager = we_getEntityManager(context);
    if (unit->frankOriginalSlot)
    {
        const u16 slot = unit->frankOriginalSlot - 1;
        if (slot < WOR_UNIT_COUNT && manager->originalEntities[slot] == entity)
            return &manager->originalUnits[slot];
        logError("original kernel: stale record for entity %u", entity->id);
        return NULL;
    }
    for (u16 slot = 0; slot < WOR_UNIT_COUNT; slot++)
    {
        if (!(manager->originalUnits[slot].flags & WOR_FREE)) continue;
        WarOriginalCall call = { .context = context, .entity = entity };
        WarOriginalRuntime runtime = wos_runtime(&call);
        WarOriginalUnit* original = &manager->originalUnits[slot];
        const u8 type = (u8)wu_toOriginalType(unit->type);
        *original = (WarOriginalUnit){
            .type = type, .owner = unit->player,
            .statusTicks = type >= 10 && type <= 14 ? 40 : wu_isSummonUnitType(unit->type) ? 45 : 0,
            .postType = (u16)wu_toOriginalType(unit->type),
            .postX = unit->tilex, .postY = unit->tiley,
            .flags = wrng_levelLoading ? 0x480 : 0,
            .pending = WOR_NONE, .next = WOR_NONE
        };
        if (wor_replace(&runtime, slot, unit->player == 4 ? 29 : 2) == WOR_ERROR)
        {
            original->flags = WOR_FREE;
            return NULL;
        }
        original->wait = (u8)(1 + unit->frankDelay);
        manager->originalEntities[slot] = entity;
        unit->frankOriginalSlot = slot + 1;
        unit->frankIdleOn = true;
        return original;
    }
    logError("original kernel: all %u records occupied; entity %u has no script record",
             WOR_UNIT_COUNT, entity->id);
    return NULL;
}

void wos_remove(WarContext* context, WarEntity* entity)
{
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    if (!unit || !unit->frankOriginalSlot) return;
    WarEntityManager* manager = we_getEntityManager(context);
    const u16 slot = unit->frankOriginalSlot - 1;
    if (slot >= WOR_UNIT_COUNT || manager->originalEntities[slot] != entity)
    {
        logError("original kernel: invalid removal for entity %u", entity->id);
        return;
    }
    if (!(manager->originalUnits[slot].flags & (WOR_DYING | WOR_REMOVED | WOR_PLACEHOLDER)))
        manager->originalUnits[slot].flags = WOR_FREE;
    manager->originalEntities[slot] = NULL;
    unit->frankOriginalSlot = 0;
}

bool wos_select(WarContext* context, WarEntity* entity, u8 order, u8 wait)
{
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return false;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    original->type = (u8)wu_toOriginalType(unit->type);
    original->owner = unit->player;
    if (wor_replace(&runtime, unit->frankOriginalSlot - 1, order) != WOR_CHANGED) return false;
    original->wait = wait;
    unit->frankIdleOn = order == 2 || order == 29;
    return true;
}

void wos_beginDeath(WarContext* context, WarEntity* entity)
{
    if (!wai_drivesTicks(context)) return;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    if (!unit || !unit->frankOriginalSlot || wu_isCorpseUnitType(unit->type)) return;
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original || (original->flags & WOR_INACTIVE)) return;
    if (!wos_select(context, entity, 1, 1)) return;
    if (original->type >= 32 && original->type <= 50)
        wai_noteBuildingDeath(context, original->owner, original->type == 48 || original->type == 49);
    original->flags = (u16)((original->flags | WOR_DYING) & ~(WOR_HELD | WOR_HOLD_REQUEST));
    original->workFlags = unit->amount ? 0x20 : 0;
    if (original->type == 16 || (original->type == 15 && original->owner == 4))
    {
        original->flags &= (u16)~WOR_DYING;
        wos_select(context, entity, 0, 1);
        original->flags |= WOR_REMOVED | WOR_INSIDE;
    }
}

static void wos_hideCorpse(WarContext* context, u16 slot)
{
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 i = 1; i < MAX_ENTITIES_COUNT; i++)
    {
        WarEntity* visual = &manager->entities[i];
        if (!visual->id || !wu_isCorpseUnit(context, visual)) continue;
        if (we_getUnitComponent(context, visual)->frankCorpseSlot == slot + 1)
        {
            we_removeEntityById(context, visual->id);
            return;
        }
    }
}

static WarOrderResult wos_deathOrder(WarOriginalRuntime* runtime, u16 slot)
{
    WarOriginalCall* call = runtime->context;
    WarOriginalUnit* original = &runtime->units[slot];
    if (original->current == 1)
    {
        // (0x13F68: a type-23 unit's end shows a strike, 0x11EC0)
        if (original->type == 23) wft_poolVisual(call->context, 7, call->context->map ?
            we_getEntityManager(call->context)->originalEntities[slot] : NULL);
        return wor_finishDeath(runtime, slot) == WOR_ERROR ? WOR_ERROR : WOR_UNCHANGED;
    }
    if (original->current != 0)
    {
        wos_error(runtime, slot, "invalid death order");
        return WOR_ERROR;
    }
    if (original->type == 51) wos_hideCorpse(call->context, slot);
    if (wai_isComputerPlayer(call->context, original->owner) && (original->flags & 0x400))
    {
        original->flags = WOR_PLACEHOLDER;
        if (original->type >= 32 && original->type <= 50) original->placeholderTicks = 255;
    }
    else
    {
        original->flags = WOR_FREE;
        original->owner = 4;
    }
    WarEntityManager* manager = we_getEntityManager(call->context);
    if (manager->originalEntities[slot])
        we_getUnitComponent(call->context, manager->originalEntities[slot])->frankOriginalSlot = 0;
    manager->originalEntities[slot] = NULL;
    return WOR_UNCHANGED;
}

bool wos_deathTick(WarContext* context, u16 slot)
{
    WarEntityManager* manager = we_getEntityManager(context);
    WarOriginalUnit* original = &manager->originalUnits[slot];
    if (!(original->flags & (WOR_DYING | WOR_REMOVED | WOR_PLACEHOLDER))) return false;
    WarOriginalCall call = { .context = context, .entity = manager->originalEntities[slot] };
    WarOriginalRuntime runtime = wos_runtime(&call);
    runtime.order = wos_deathOrder;
    const u8 speed = (u8)(context->map->players[original->owner].upgrades[2].level & 15);
    wor_unitTick(&runtime, slot, speed, (original->workFlags & 0x20) != 0);
    return true;
}

WarOrderResult wos_takePost(WarContext* context, WarEntity* entity, bool* alerted)
{
    if (!wos_unit(context, entity)) return WOR_ERROR;
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    return wor_takePost(&runtime, unit->frankOriginalSlot - 1, alerted);
}

bool wos_startPair(WarContext* context, WarEntity* entity, u8 pending, u8 next)
{
    if (!wos_unit(context, entity)) return false;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    const u16 slot = unit->frankOriginalSlot - 1;
    if (wor_queue(&runtime, slot, pending, next) != WOR_CHANGED) return false;
    if (wor_consume(&runtime, slot, false) != WOR_CHANGED) return false;
    unit->frankIdleOn = pending == 2 || pending == 29;
    return true;
}

bool wos_queue(WarContext* context, WarEntity* entity, u8 pending, u8 next)
{
    if (!wos_unit(context, entity)) return false;
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    return wor_queue(&runtime, we_getUnitComponent(context, entity)->frankOriginalSlot - 1,
                     pending, next) == WOR_CHANGED;
}

WarOrderResult wos_targetSpell(WarContext* context, WarEntity* entity, bool action)
{
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return WOR_ERROR;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    WarEntityManager* manager = we_getEntityManager(context);
    WarEntity* target = original->target && original->target <= WOR_UNIT_COUNT ?
        manager->originalEntities[original->target - 1] : NULL;
    // (the recipient died between the checkpoint and the script's action:
    // its record is gone, as the original's cleared target; nothing to cast on)
    if (!target) return WOR_UNCHANGED;
    WarUnitComponent* other = we_getUnitComponent(context, target);
    u8 mana = (u8)unit->mana;
    u16 health = (u16)other->hp;
    WarOrderResult result;
    switch (original->current)
    {
        case 17:
            result = wor_healing(&runtime, unit->frankOriginalSlot - 1, &mana, &health,
                (u16)wu_getUnitStats(other->type)->hp, action);
            break;
        case 19:
            result = wor_invisibility(&runtime, unit->frankOriginalSlot - 1, &mana, action);
            break;
        case 21:
            result = wor_unholyArmour(&runtime, unit->frankOriginalSlot - 1, &mana, &health, action);
            break;
        default:
            wos_error(&runtime, unit->frankOriginalSlot - 1, "invalid targeted spell order");
            return WOR_ERROR;
    }
    unit->mana = mana;
    other->hp = health;
    if (action && result == WOR_CHANGED)
    {
        WarOriginalUnit* recipient = &runtime.units[original->target - 1];
        other->invisible = recipient->invisibility != 0;
        other->invisibilityTime = recipient->invisibility * 0.03f;
        other->invulnerable = recipient->invulnerability != 0;
        other->invulnerabilityTime = recipient->invulnerability * 0.03f;
        wai_targetsDirty = true;
    }
    return result;
}

static bool wos_spawn(WarOriginalRuntime* runtime, u16 slot, u8 type)
{
    WarOriginalCall* call = runtime->context;
    WarContext* context = call->context;
    WarEntity* caster = we_getEntityManager(context)->originalEntities[slot];
    if (!caster)
    {
        wos_error(runtime, slot, "missing summoning caster");
        return false;
    }
    vec2 tile = wu_getUnitCenterTile(context, caster), spawn;
    if (!wmv_findExit(context, caster, (s32)tile.x, (s32)tile.y, &spawn)) return false;
    u16 freeSlot = 0;
    while (freeSlot < WOR_UNIT_COUNT && !(runtime->units[freeSlot].flags & WOR_FREE)) freeSlot++;
    if (freeSlot == WOR_UNIT_COUNT) return false;
    WarEntity* summoned = we_createUnit(context, CREATE_UNIT_ARGS_INIT(
        .type = wu_fromOriginalType(type), .x = (s32)spawn.x, .y = (s32)spawn.y,
        .player = runtime->units[slot].owner, .addToMap = true));
    if (!summoned) return false;
    we_setInitialIdleState(context, summoned);
    wmv_occupy(context, summoned);   // (0x2E7E4 places it, 0x13928)
    if (type >= 19 && type <= 31) wft_poolVisual(context, 8, summoned);   // (0x2E740)
    vec2 size = wu_getUnitSize(context, summoned);
    wpath_setStaticEntity(&context->map->finder, (s32)spawn.x, (s32)spawn.y,
                         (s32)size.x, (s32)size.y, summoned->id);
    WarEntity* effect = we_createEntity(context, WAR_ENTITY_TYPE_ANIMATION, true);
    if (effect)
    {
        vec2 position = wu_getUnitCenterPosition(context, summoned);
        we_addAnimationsComponent(context, effect);
        wanim_createSpellAnimation(context, effect, position);
        wa_createAudioWithPosition(context, CREATE_AUDIO_ARGS_INIT(
            .audioId=WAR_NORMAL_SPELL, .position=position, .hasPosition=true, .loop=false));
    }
    return true;
}

WarOrderResult wos_summon(WarContext* context, WarEntity* entity, bool action)
{
    if (!wos_unit(context, entity)) return WOR_ERROR;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    u8 mana = (u8)unit->mana;
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    runtime.summon = wos_spawn;
    WarOrderResult result = wor_summon(&runtime, unit->frankOriginalSlot - 1, &mana, action);
    we_getUnitComponent(context, entity)->mana = mana;
    return result;
}

static bool wos_raiseCorpse(WarOriginalRuntime* runtime, u16 caster, u16 corpse)
{
    WarOriginalCall* call = runtime->context;
    WarContext* context = call->context;
    const WarOriginalUnit* remains = &runtime->units[corpse];
    u16 freeSlot = 0;
    while (freeSlot < WOR_UNIT_COUNT && !(runtime->units[freeSlot].flags & WOR_FREE)) freeSlot++;
    if (freeSlot == WOR_UNIT_COUNT ||
        !wmv_placeFree(context, WAR_UNIT_SKELETON, remains->tileX, remains->tileY)) return false;
    WarEntity* skeleton = we_createUnit(context, CREATE_UNIT_ARGS_INIT(
        .type = WAR_UNIT_SKELETON, .x = remains->tileX, .y = remains->tileY,
        .player = runtime->units[caster].owner, .addToMap = true));
    if (!skeleton) return false;
    wu_setUnitDirection(context, skeleton, (WarDirection)remains->direction);
    wos_unit(context, skeleton)->direction = remains->direction;
    we_setInitialIdleState(context, skeleton);
    wpath_setStaticEntity(&context->map->finder, remains->tileX, remains->tileY, 1, 1, skeleton->id);
    wos_hideCorpse(context, corpse);
    return true;
}

WarOrderResult wos_resurrect(WarContext* context, WarEntity* entity, bool action)
{
    if (!wos_unit(context, entity)) return WOR_ERROR;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    u8 mana = (u8)unit->mana;
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    runtime.resurrect = wos_raiseCorpse;
    WarOrderResult result = wor_resurrect(&runtime, unit->frankOriginalSlot - 1, &mana, action);
    we_getUnitComponent(context, entity)->mana = mana;
    return result;
}

WarOrderResult wos_shotMana(WarContext* context, WarEntity* entity, bool action)
{
    if (!wos_unit(context, entity)) return WOR_ERROR;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    u8 mana = (u8)unit->mana;
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    WarResource* level = wres_getOrCreateResource(context, context->map->levelInfoIndex);
    WarOrderResult result = wor_shotMana(&runtime, unit->frankOriginalSlot - 1,
        level->levelInfo.ai->alliance[unit->player], &mana, action);
    unit->mana = mana;
    return result;
}

void wos_tickStatuses(WarContext* context)
{
    WarEntityManager* manager = we_getEntityManager(context);
    for (u16 slot = 0; slot < WOR_UNIT_COUNT; slot++)
    {
        WarEntity* entity = manager->originalEntities[slot];
        if (entity)
        {
            WarUnitComponent* unit = we_getUnitComponent(context, entity);
            if (!unit->invisible) manager->originalUnits[slot].invisibility = 0;
            if (!unit->invulnerable) manager->originalUnits[slot].invulnerability = 0;
        }
    }
    WarOriginalCall call = { .context = context };
    WarOriginalRuntime runtime = wos_runtime(&call);
    WarResource* level = wres_getOrCreateResource(context, context->map->levelInfoIndex);
    wor_tickSpellTimers(&runtime);
    for (u16 slot = 0; slot < WOR_UNIT_COUNT; slot++)
    {
        WarEntity* entity = manager->originalEntities[slot];
        if (!entity) continue;
        WarUnitComponent* unit = we_getUnitComponent(context, entity);
        unit->invisible = manager->originalUnits[slot].invisibility != 0;
        unit->invisibilityTime = manager->originalUnits[slot].invisibility * 0.03f;
        unit->invulnerable = manager->originalUnits[slot].invulnerability != 0;
        unit->invulnerabilityTime = manager->originalUnits[slot].invulnerability * 0.03f;
        if (manager->originalUnits[slot].type >= 10 && manager->originalUnits[slot].type <= 14)
        {
            u8 mana = (u8)unit->mana;
            wor_regenerateMana(&runtime, slot, level->levelInfo.ai->alliance[unit->player], &mana);
            unit->mana = mana;
        }
        else if (wu_isSummonUnitType(unit->type))
        {
            u8 mana = (u8)unit->mana;
            u16 health = (u16)unit->hp;
            bool dead;
            if (wor_decay(&runtime, slot, &mana, &health, &dead) != WOR_CHANGED) continue;
            unit->mana = mana;
            unit->hp = health;
            if (dead)
            {
                WarStateDeath* death = wst_createDeathState(context, entity);
                if (!death || !wst_resetState(context, entity, (WarStateBase*)death, WAR_TRANSITION_CAUSE_LIFECYCLE))
                    logError("original kernel: failed decay death for entity %u", entity->id);
            }
        }
    }
}

WarOrderResult wos_takeNext(WarContext* context, WarEntity* entity)
{
    if (!wos_unit(context, entity)) return WOR_ERROR;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    WarOrderResult result = wor_consume(&runtime, unit->frankOriginalSlot - 1, true);
    if (result == WOR_CHANGED)
    {
        const u8 order = runtime.units[unit->frankOriginalSlot - 1].current;
        unit->frankIdleOn = order == 2 || order == 29;
    }
    return result;
}

static const WarEntity* wos_workEntity;

bool wos_atWorkCheckpoint(const WarEntity* entity)
{
    return wos_workEntity == entity;
}

void wos_workHandler(WarContext* context, WarEntity* entity, WarStateBase* state)
{
    const WarEntity* previous = wos_workEntity;
    wos_workEntity = entity;
    switch (state->type)
    {
        case WAR_STATE_GOLD: wst_updateGatherGoldState(context, entity, state); break;
        case WAR_STATE_WOOD: wst_updateGatherWoodState(context, entity, state); break;
        case WAR_STATE_DELIVER: wst_updateDeliverState(context, entity, state); break;
        case WAR_STATE_MINING: wst_updateMiningState(context, entity, state); break;
        default: logError("original kernel: invalid worker handler %d", state->type); break;
    }
    wos_workEntity = previous;
}

static void wos_syncTile(WarContext* context, WarEntity* entity, WarOriginalUnit* original)
{
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    vec2 tile = wu_isBuildingUnitType(unit->type) ? vec2i(unit->tilex, unit->tiley)
        : wu_getUnitCenterTile(context, entity);
    original->tileX = (s16)tile.x;
    original->tileY = (s16)tile.y;
    original->owner = unit->player;
#ifndef FRANK_BOARD
    if (getenv("WAR_TILECHECK") && ((u32)tile.x >= MAP_TILES_WIDTH || (u32)tile.y >= MAP_TILES_HEIGHT))
    {
        WarStateBase* st = wst_getActiveState(context, entity);
        vec2 pos = wu_getUnitCenterPosition(context, entity);
        fprintf(stderr, "off-map tick %u entity %u type %d tile %d,%d pos %.1f,%.1f state %d\n", wrng_tick, entity->id,
                unit->type, (s32)tile.x, (s32)tile.y, pos.x, pos.y, st ? (s32)st->type : -1);
    }
#endif
}

static u16 wos_reference(WarContext* context, WarEntityId id, u16 retained)
{
    if (!id) return 0;
    WarEntity* entity = we_findEntity(context, id);
    if (entity && wu_isUnit(entity))
    {
        WarUnitComponent* unit = we_getUnitComponent(context, entity);
        if (unit->frankOriginalSlot)
        {
            WarOriginalUnit* original = &we_getEntityManager(context)->originalUnits[unit->frankOriginalSlot - 1];
            if (!(original->flags & WOR_INACTIVE))
            {
                if (wst_isInsideBuilding(context, entity)) original->flags |= WOR_INSIDE;
                else original->flags &= (u16)~WOR_INSIDE;
                if (!unit->invisible) original->invisibility = 0;
            }
            return unit->frankOriginalSlot;
        }
    }
    if (!retained) logError("original kernel: entity %u has no retained target record", id);
    return retained;
}

static u16 wos_occupant(WarOriginalRuntime* runtime, s16 x, s16 y)
{
    WarOriginalCall* call = runtime->context;
    WarEntityManager* manager = we_getEntityManager(call->context);
    for (u16 slot = 0; slot < WOR_UNIT_COUNT; slot++)
    {
        WarEntity* entity = manager->originalEntities[slot];
        WarOriginalUnit* original = &runtime->units[slot];
        if (!entity || (original->flags & (WOR_INACTIVE | WOR_INSIDE))) continue;
        WarUnitComponent* unit = we_getUnitComponent(call->context, entity);
        wos_syncTile(call->context, entity, original);
        original->owner = unit->player;
        if (x >= original->tileX && x < original->tileX + unit->sizex &&
            y >= original->tileY && y < original->tileY + unit->sizey) return slot + 1;
    }
    return 0;
}

bool wos_areaTarget(WarContext* context, WarEntity* entity, s16 tx, s16 ty, s16* x, s16* y)
{
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return false;
    wos_syncTile(context, entity, original);
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    runtime.occupant = wos_occupant;
    return wor_areaTarget(&runtime, we_getUnitComponent(context, entity)->frankOriginalSlot - 1,
                          tx, ty, x, y) == WOR_CHANGED;
}

WarOrderResult wos_checkCatapult(WarContext* context, WarEntity* entity, WarEntity* target)
{
    WarOriginalUnit* original = wos_unit(context, entity);
    WarOriginalUnit* other = wos_unit(context, target);
    if (!original || !other) return WOR_ERROR;
    wos_syncTile(context, entity, original);
    wos_syncTile(context, target, other);
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    WarUnitComponent* recipient = we_getUnitComponent(context, target);
    original->target = recipient->frankOriginalSlot;
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    runtime.occupant = wos_occupant;
    WarResource* level = wres_getOrCreateResource(context, context->map->levelInfoIndex);
    return wor_checkCatapult(&runtime, unit->frankOriginalSlot - 1,
        level->levelInfo.ai->alliance[unit->player], 3, (u8)recipient->sizex, (u8)recipient->sizey);
}

// 0x2216C on a move's own target: false (and the target cleared, with a
// matching attacker) when it is gone, inside or invisible
static bool wos_moveTargetValid(WarContext* context, WarEntity* entity, WarEntity* target)
{
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return false;
    original->target = wos_reference(context, target ? target->id : 0, original->target);
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    return wor_validateTarget(&runtime, we_getUnitComponent(context, entity)->frankOriginalSlot - 1);
}

bool wos_validateTarget(WarContext* context, WarEntity* entity,
                       WarEntityId* target, WarEntityId* attacker)
{
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return false;
    original->target = wos_reference(context, *target, original->target);
    original->attacker = wos_reference(context, *attacker, original->attacker);
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    const bool valid = wor_validateTarget(&runtime, we_getUnitComponent(context, entity)->frankOriginalSlot - 1);
    WarEntityManager* manager = we_getEntityManager(context);
    if (!original->target) *target = 0;
    else if (manager->originalEntities[original->target - 1])
        *target = manager->originalEntities[original->target - 1]->id;
    if (!original->attacker) *attacker = 0;
    else if (manager->originalEntities[original->attacker - 1])
        *attacker = manager->originalEntities[original->attacker - 1]->id;
    return valid;
}

void wos_explore(WarContext* context, WarEntity* entity)
{
    if (!wai_drivesTicks(context)) return;
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    wos_syncTile(context, entity, original);
    if (original->current >= 4 && original->current <= 8)
    {
        WarStateAttack* attack = (WarStateAttack*)wst_findStateInStack(context, entity, WAR_STATE_ATTACK);
        original->target = wos_reference(context, attack ? attack->targetEntityId : 0, original->target);
    }
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    wor_explore(&runtime, &we_getEntityManager(context)->originalVision, unit->frankOriginalSlot - 1,
                (u8)unit->sizex, (u8)unit->sizey, unit->player == 0, false);
}

void wos_refreshVisibility(WarContext* context)
{
    WarEntityManager* manager = we_getEntityManager(context);
    for (u16 slot = 0; slot < WOR_UNIT_COUNT; slot++)
    {
        WarEntity* entity = manager->originalEntities[slot];
        if (!entity || !wu_isUnit(entity)) continue;
        WarUnitComponent* unit = we_getUnitComponent(context, entity);
        WarOriginalUnit* original = &manager->originalUnits[slot];
        if (!(original->flags & WOR_INACTIVE))
        {
            wos_syncTile(context, entity, original);
            if (wst_isInsideBuilding(context, entity)) original->flags |= WOR_INSIDE;
            else original->flags &= (u16)~WOR_INSIDE;
        }
        WarOriginalCall call = { .context = context, .entity = entity };
        WarOriginalRuntime runtime = wos_runtime(&call);
        wor_refreshVisibility(&runtime, &manager->originalVision, slot, (u8)unit->sizex,
                              (u8)unit->sizey, !context->map->fowEnabled);
    }
}

bool wos_attackZone(WarContext* context, s16* x, s16* y)
{
    WarEntityManager* manager = we_getEntityManager(context);
    for (u16 slot = 0; slot < WOR_UNIT_COUNT; slot++)
        if (manager->originalEntities[slot] && !(manager->originalUnits[slot].flags & WOR_INACTIVE))
            wos_syncTile(context, manager->originalEntities[slot], &manager->originalUnits[slot]);
    WarOriginalCall call = { .context = context };
    WarOriginalRuntime runtime = wos_runtime(&call);
    return wor_attackZone(&runtime, x, y) == WOR_CHANGED;
}

WarOrderResult wos_tick(WarContext* context, WarEntity* entity,
                       void (*action)(WarContext*, WarEntity*))
{
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return WOR_ERROR;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    if (wai_drivesTicks(context))
    {
#ifndef FRANK_BOARD
        {
            // WAR_SCRIPTWATCH=id: where each script of that unit runs from
            static s32 watch = -2;
            if (watch == -2) watch = getenv("WAR_SCRIPTWATCH") ? atoi(getenv("WAR_SCRIPTWATCH")) : -1;
            if ((s32)entity->id == watch)
            {
                extern int backtrace(void**, int);
                extern void backtrace_symbols_fd(void* const*, int, int);
                void* frames[8];
                fprintf(stderr, "script tick %u entity %u pc %u orders %u/%u/%u\n", wrng_tick, entity->id, original->pc, original->current, original->pending, original->next);
                backtrace_symbols_fd(frames, backtrace(frames, 8), 2);
            }
        }
#endif
        if (unit->frankScriptTick == wrng_tick)
        {
            logError("original kernel: two scripts in tick %u for entity %u", wrng_tick, entity->id);
#ifndef FRANK_BOARD
            if (getenv("WAR_KERNELTRACE"))
            {
                extern int backtrace(void**, int);
                extern void backtrace_symbols_fd(void* const*, int, int);
                void* frames[24];
                backtrace_symbols_fd(frames, backtrace(frames, 24), 2);
            }
#endif
            return WOR_ERROR;
        }
        unit->frankScriptTick = wrng_tick;
    }
    vec2 position = wu_getUnitCenterPosition(context, entity);
    original->pixelX = (s16)position.x;
    original->pixelY = (s16)position.y;
    wos_syncTile(context, entity, original);
    original->direction = unit->wmvStepping ? unit->wmvDir : (u8)unit->direction;
    original->owner = unit->player;
    if (!unit->invisible) original->invisibility = 0;
    if (unit->frankOrderQueued) original->flags |= WOR_SUPPRESS_ACTION;
    WarOriginalCall call = { context, entity, action };
    WarOriginalRuntime runtime = wos_runtime(&call);
    const u16 slot = unit->frankOriginalSlot - 1;
    const u8 speed = (u8)(context->map->players[unit->player].upgrades[2].level & 15);
    WarOrderResult result = wor_scriptTick(&runtime, slot, speed, unit->amount > 0);
    if (result != WOR_ERROR)
    {
        wos_publish(&runtime, slot);
        if (result == WOR_CHANGED) wos_explore(context, entity);
#ifndef FRANK_BOARD
        we_getEntityManager(context)->originalScriptTicks[original->current]++;
#endif
    }
    return result;
}
