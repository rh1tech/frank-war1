// frank-war1 — the original's blows and missiles, tick by tick (included by
// war_state_machine_attack.c; port/war_ai_frank.c's tick runs them).
//
// WarCraft 1.20 strikes at the attack script's action (op 9, 0x106EC): a
// melee unit rolls the miss there (0x104B0: one in five, from its random
// numbers); an archer, a caster or a catapult launches a missile (0x11A10)
// that has rolled it already, and aims off by up to 7 pixels (15 on a miss)
// each way, two more draws. The missile flies a set number of pixels a tick
// (arrows 5, fireballs 4, rocks 2), one random draw at each step, and
// strikes its target when its run is over, wherever it went. Rocks instead
// damage the impact's occupied tiles, independently of the damage roll
// (0x12240). Casters pay mana for each shot: 5 for the human, 1 for the computer.
// war1's attack state still chooses and follows targets and animates; the
// timing, the roll and the damage are the original's here, in its order:
// the unit table, then the missile table, every tick.
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib

#define WFT_MISSILES WOR_EFFECT_COUNT
extern bool wai_isComputerPlayer(WarContext* context, s32 player);

// the original's missile type of a unit (0x51D9C): 0 fireball, 1 rock,
// 2 arrow, 10 water, 12 none (melee)
static s32 wft_missileType(WarUnitType type)
{
    switch (type)
    {
        case WAR_UNIT_CATAPULT_HUMANS: case WAR_UNIT_CATAPULT_ORCS: return 1;
        case WAR_UNIT_ARCHER: case WAR_UNIT_SPEARMAN: return 2;
        case WAR_UNIT_CONJURER: case WAR_UNIT_WARLOCK: case WAR_UNIT_CLERIC:
        case WAR_UNIT_NECROLYTE: case WAR_UNIT_MEDIVH: return 0;
        case WAR_UNIT_WATER_ELEMENTAL: return 10;
        case WAR_UNIT_TOWER_HUMANS: case WAR_UNIT_TOWER_ORCS: return 2;
        default: return 12;
    }
}

typedef WarOriginalEffect WarFrankMissile;

static WarFrankMissile wft_missiles[WFT_MISSILES];
static WarMap* wft_map;
static s8 wft_windX, wft_windY;

// 0x104B0: the blow's damage; a miss (one in five) is nothing. With no unit
// to strike, terrain takes full unarmoured damage without a draw.
static s32 wft_roll(WarContext* context, WarUnitComponent* u, WarEntity* target)
{
    if (context->map->players[u->player].godMode) return 200;
    const WarUnitStats* stats = wu_getUnitStats(u->type);
    s32 normal = stats->rndDamage, piercing = stats->minDamage;
    const bool archer = u->type == WAR_UNIT_ARCHER || u->type == WAR_UNIT_SPEARMAN;
    const s32 bonus = 2 * context->map->players[u->player].upgrades[archer ? 0 : 1].level;
    if (archer) piercing += bonus;
    else normal += bonus;
    if (!target)
    {
        if (!u->frankOriginalSlot) return 0;
        WarOriginalUnit* original = &we_getEntityManager(context)->originalUnits[u->frankOriginalSlot - 1];
        return wmv_hasTerrain(original->goalX, original->goalY, 0xC)
            ? MIN(normal + piercing, 255) : 0;
    }
    if (!wu_isUnit(target)) return 0;
#ifndef FRANK_BOARD
    if (getenv("WAR_TURNDEBUG")) fprintf(stderr, "roll by type %d player %d at %d\n", u->type, u->player, (s32)target->id);
#endif
    if (wrng_next() >= 0x6666) return 0;
    WarUnitComponent* tu = we_getUnitComponent(context, target);
    const s32 armor = wu_isBuildingUnitType(tu->type) ? wu_getBuildingStats(tu->type)->armor
        : wu_getUnitStats(tu->type)->armor;
    const s32 shield = 2 * context->map->players[tu->player].upgrades[9].level;
    return MIN(piercing + MAX(normal - armor - shield, 0), 255);
}

static void wft_strike(WarContext* context, WarEntity* attacker, WarEntity* target, s32 damage)
{
    if (!target || !wu_isUnit(target)) return;
    WarUnitComponent* tu = we_getUnitComponent(context, target);
    if (!tu || tu->hp <= 0 || wst_isDead(context, target) || wst_isCollapsing(context, target)) return;
    if (attacker)
    {
        extern void wai_noteAttacker(WarContext* context, WarEntity* target, WarEntity* attacker);
        wai_noteAttacker(context, target, attacker);
    }
    if (damage <= 0) return;
    // (we_takeDamage takes min + max(rnd - armour, 0): the rolled damage, whole)
    const s32 armor = tu->armor;
    tu->armor = 0;
    we_takeDamage(context, target, damage, 0);
    tu->armor = armor;
    if (tu->hp <= 0 && attacker)
    {
        extern bool wai_validateCombatTarget(WarContext*, WarEntity*);
        wai_validateCombatTarget(context, attacker);
    }
    wai_targetsDirty = true;
}

static void wft_poisonDamage(WarOriginalRuntime* runtime, u16 source, u16 target, u8 damage)
{
    WarOriginalCall* call = runtime->context;
    WarEntityManager* manager = we_getEntityManager(call->context);
    wft_strike(call->context, source ? manager->originalEntities[source - 1] : NULL,
               manager->originalEntities[target - 1], damage);
}

static void wft_poisonVisual(WarOriginalRuntime* runtime, WarOriginalEffect* effect)
{
    WarOriginalCall* call = runtime->context;
    WarEntity* visual = we_createEntity(call->context, WAR_ENTITY_TYPE_ANIMATION, true);
    if (!visual)
    {
        logError("original kernel: failed poison visual allocation");
        return;
    }
    effect->visual = visual->id;
    we_addAnimationsComponent(call->context, visual);
    wanim_createPoisonCloudAnimation(call->context, visual, vec2i(effect->x, effect->y));
    WarSpriteAnimation* animation = &we_getAnimationsComponent(call->context, visual)->animations.items[0];
    S32ListClear(&animation->frames);
    wanim_addAnimationFrame(animation, effect->frame);
}

static WarOrderResult wft_poison(WarContext* context, WarEntity* entity, bool action)
{
    if (!wos_unit(context, entity)) return WOR_ERROR;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    u8 mana = (u8)unit->mana;
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wos_runtime(&call);
    runtime.effects = wft_missiles;
    runtime.effectCreated = wft_poisonVisual;
    WarOrderResult result = wor_poison(&runtime, unit->frankOriginalSlot - 1, &mana, action);
    we_getUnitComponent(context, entity)->mana = mana;
    return result;
}

static void wft_terrainDamage(WarOriginalRuntime* runtime, s16 x, s16 y, u8 damage)
{
    WarOriginalCall* call = runtime->context;
    wmv_damageTerrain(call->context, x, y, damage);
}

// war1's rain projectile shows one fall of a record (it strikes nothing:
// the strike is the record's, 0x12338)
static void wft_rainFall(WarContext* context, const WarOriginalEffect* effect)
{
    wproj_createProjectile(context, WAR_PROJECTILE_RAIN_OF_FIRE, 0, 0,
                           vec2i(effect->x, effect->goalY - 120), vec2i(effect->x, effect->goalY));
}

static void wft_rainCreated(WarOriginalRuntime* runtime, WarOriginalEffect* effect)
{
    WarOriginalCall* call = runtime->context;
    if (effect->type == 4) wft_rainFall(call->context, effect);
}

// The runtime for the shared pool's records.
static WarOriginalRuntime wft_effectRuntime(WarOriginalCall* call)
{
    WarOriginalRuntime runtime = wos_runtime(call);
    runtime.effects = wft_missiles;
    runtime.occupant = wos_occupant;
    runtime.damage = wft_poisonDamage;
    runtime.terrainDamage = wft_terrainDamage;
    runtime.effectCreated = wft_rainCreated;
    return runtime;
}

// 0x11E70 at a unit (0x11EC0, 0x2E740): a short visual record of the
// shared pool; war1 draws its own presentation
void wft_poolVisual(WarContext* context, u8 type, WarEntity* at)
{
    if (!wai_drivesTicks(context) || !at) return;
    if (context->map != wft_map)
    {
        wft_map = context->map;
        memset(wft_missiles, 0, sizeof(wft_missiles));
    }
    const vec2 p = wu_getUnitCenterPosition(context, at);
    WarOriginalCall call = { .context = context };
    WarOriginalRuntime pool = wft_effectRuntime(&call);
    pool.effectCreated = NULL;
    wor_impactVisual(&pool, type, (s16)p.x, (s16)p.y);
}

static WarOrderResult wft_rain(WarContext* context, WarEntity* entity, bool action)
{
    if (!wos_unit(context, entity)) return WOR_ERROR;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    u8 mana = (u8)unit->mana;
    WarOriginalCall call = { .context = context, .entity = entity };
    WarOriginalRuntime runtime = wft_effectRuntime(&call);
    WarOrderResult result = wor_rain(&runtime, unit->frankOriginalSlot - 1, &mana, action);
    we_getUnitComponent(context, entity)->mana = mana;
    return result;
}

static void wft_windTick(WarContext* context)
{
    WarOriginalCall call = { .context = context };
    WarOriginalRuntime runtime = wos_runtime(&call);
    wor_tickWind(&runtime, &wft_windX, &wft_windY);
}

static void wft_launch(WarContext* context, WarEntity* e, WarUnitComponent* u, WarEntity* target, s32 mtype)
{
    WarOriginalUnit* original = wos_unit(context, e);
    if (target ? !wu_isUnit(target) : !original || original->current != 7) return;
#ifndef FRANK_BOARD
    if (getenv("WAR_FIGHTDEBUG")) fprintf(stderr, "launch t %.3f by %d (type %d) at %d\n", context->gameTime, (s32)e->id, u->type, target ? (s32)target->id : 0);
#endif
    s32 slot = -1;
    for (s32 i = 0; i < WFT_MISSILES; i++) if (!wft_missiles[i].used) { slot = i; break; }
    if (slot < 0) return;
    WarFrankMissile* m = &wft_missiles[slot];
    memset(m, 0, sizeof(*m));
    m->used = true;
    m->type = (u8)mtype;
    m->shooter = e->id;
    m->target = target ? target->id : 0;
    // footprint centres, in pixels (the original's pixel + 0x595C0)
    vec2 sp = wu_getUnitCenterPosition(context, e);
    vec2 tp = target ? wu_getUnitCenterPosition(context, target)
        : vec2i(original->goalX * 16 + 8, original->goalY * 16 + 8);
    const s32 sx = (s32)sp.x, sy = (s32)sp.y;
    s32 tx = (s32)tp.x, ty = (s32)tp.y;
    m->damage = (u8)wft_roll(context, u, target);
    // the aim: off by up to b/2 each way, b no more than half the distance
    static const u8 missSpread[12] = { 15, 31, 15, 0, 0, 0, 0, 0, 0, 0, 15, 15 };
    static const u8 hitSpread[12] = { 7, 15, 7, 0, 0, 0, 0, 0, 0, 0, 7, 7 };
    static const u8 minRun[12] = { 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    s32 b = m->damage ? hitSpread[mtype] : missSpread[mtype];
    const s32 dist = MAX(ABS(sx - tx), ABS(sy - ty));
    if (b >= dist / 2) b = dist / 2;
    tx += (wrng_next() & b) - b / 2;
    ty += (wrng_next() & b) - b / 2;
    m->x = (s16)sx;
    m->y = (s16)sy;
    wor_lineInit(&m->line, sx, sy, tx, ty);
    s32 run = MAX(ABS(sx - tx), ABS(sy - ty));
    if (target) run = MAX(run, minRun[mtype] * 16);
    m->run = (s16)(run & 0x1FFF);
    m->step = (u8)(run / 5);
    // its script: arrows and rocks wait 4 ticks before their first step,
    // the others step from the next tick (the launch tick's own pass sets
    // the wait)
    m->wait = (mtype == 1 || mtype == 2) ? 5 : 1;
    m->state = 0;
    // war1's projectile shows it (it strikes nothing: the strike is here)
    if (wu_isRangeUnitType(u->type))
        wproj_createProjectile(context, wu_getProjectileType(u->type), e->id, target ? target->id : 0,
                               sp, tp);
}

// 0x11FF0 (and 0x12074 for a rock): one step of a missile's run.
static void wft_fly(WarFrankMissile* m)
{
    static const u8 speed[12] = { 4, 2, 5, 0, 0, 0, 0, 0, 0, 0, 4, 4 };
    static const u8 arc[12] = { 0, 5, 10, 5, 0, 255, 0, 0, 168, 31, 1, 0 };
    s32 dx, dy;
    wor_lineStep(&m->line, &dx, &dy);
    m->x = (s16)(m->x + dx * speed[m->type]);
    m->y = (s16)(m->y + dy * speed[m->type]);
    const s32 left = m->run - speed[m->type];
    if (left < 0)
    {
        m->state = 6;
        // its strike comes after the end animation's waits
        m->wait = m->type == 2 ? 1 : (m->type == 10 || m->type == 11) ? 5 : 9;
    }
    else
    {
        m->run = (s16)left;
    }
#ifndef FRANK_BOARD
    if (getenv("WAR_TURNDEBUG")) fprintf(stderr, "fly missile type %d run %d state %d\n", m->type, m->run, m->state);
#endif
    wrng_next();
    if (m->state == 2 || (m->type == 1 && m->state == 6))
    {
        // a rock's arc: its height from the run left, a draw unless past
        // the table's end
        s32 d = m->run, k = 0;
        for (;;)
        {
            d -= m->step;
            if (d < 0) { wrng_next(); break; }
            k++;
            if (arc[k] == 255) break;
        }
    }
}

// A rock hits occupied tiles, not distinct units; adjacent damage is
// quartered, and a building quarters it again (0x120C8).
static void wft_rockUnitSplash(WarContext* context, WarFrankMissile* missile, WarEntity* shooter)
{
    const s32 cx = missile->x >= 0 ? missile->x / 16 : -((15 - missile->x) / 16);
    const s32 cy = missile->y >= 0 ? missile->y / 16 : -((15 - missile->y) / 16);
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 y = cy - 1; y <= cy + 1; y++)
        for (s32 x = cx - 1; x <= cx + 1; x++)
        {
            if (x < 0 || x >= MAP_TILES_WIDTH || y < 0 || y >= MAP_TILES_HEIGHT) continue;
            bool occupied = false;
            for (u16 slot = 0; slot < WOR_UNIT_COUNT; slot++)
            {
                WarEntity* target = manager->originalEntities[slot];
                if (!target || (manager->originalUnits[slot].flags & (WOR_INACTIVE | WOR_INSIDE))) continue;
                WarUnitComponent* unit = we_getUnitComponent(context, target);
                const bool building = wu_isBuildingUnitType(unit->type);
                vec2 tile = building ? vec2i(unit->tilex, unit->tiley) : wu_getUnitCenterTile(context, target);
                if (x < tile.x || y < tile.y || x >= tile.x + unit->sizex || y >= tile.y + unit->sizey) continue;
                occupied = true;
                if (target != shooter)
                {
                    s32 damage = x == cx && y == cy ? 255 : 63;
                    if (building) damage /= 4;
                    wft_strike(context, shooter, target, damage);
                }
                break;
            }
            if (!occupied) wmv_damageTerrain(context, x, y, x == cx && y == cy ? 255 : 63);
        }
}

// The missile table, after the unit table, every tick.
static void wft_missilesTick(WarContext* context)
{
    if (context->map != wft_map)
    {
        wft_map = context->map;
        memset(wft_missiles, 0, sizeof(wft_missiles));
    }
    WarOriginalCall call = { .context = context };
    WarOriginalRuntime runtime = wos_runtime(&call);
    runtime.occupant = wos_occupant;
    runtime.damage = wft_poisonDamage;
    runtime.windX = wft_windX;
    runtime.windY = wft_windY;
    for (s32 i = 0; i < WFT_MISSILES; i++)
    {
        WarFrankMissile* m = &wft_missiles[i];
        if (!m->used) continue;
        if (m->type == 3)
        {
            if (wor_poisonTick(&runtime, m) == WOR_ERROR) return;
            WarEntity* visual = m->visual ? we_findEntity(context, m->visual) : NULL;
            if (visual)
            {
                if (!m->used) we_removeEntityById(context, visual->id);
                else
                {
                    WarAnimationsComponent* animations = we_getAnimationsComponent(context, visual);
                    for (s32 k = 0; k < animations->animations.count; k++)
                    {
                        WarSpriteAnimation* animation = &animations->animations.items[k];
                        animation->offset = vec2f(m->x - animation->sprite.frameWidth * 0.5f,
                                                  m->y - animation->sprite.frameHeight * 0.5f);
                        animation->frames.items[0] = m->frame;
                    }
                }
            }
            continue;
        }
        if (m->type == 4 || m->type == 7 || m->type == 8)
        {
            // rain and impacts run their own scripts (0x12338, 0x11E70)
            WarOriginalRuntime pool = wft_effectRuntime(&call);
            const bool struck = m->type == 4 && (m->step & 0x80);
            if (wor_effectTick(&pool, m) == WOR_ERROR) return;
            if (struck && m->used && !(m->step & 0x80) && m->step < 3) wft_rainFall(context, m);
            continue;
        }
        if (--m->wait > 0) continue;
        m->wait = 1;
        switch (m->state)
        {
            case 0:
                // 0x11FA8: on its way (a rock on its arc)
                m->state = m->type == 1 ? 2 : 1;
                m->wait = 1;
                break;
            case 1:
            case 2:
                wft_fly(m);
                break;
            case 6:
            {
                // 0x12240: targeted strike, or a rock's impact area.
                WarEntity* shooter = we_findEntity(context, m->shooter);
                WarEntity* target = we_findEntity(context, m->target);
                if (m->type == 1)
                {
                    wft_rockUnitSplash(context, m, shooter);
                    // its impact takes a record of its own (0x12240)
                    WarOriginalRuntime pool = wft_effectRuntime(&call);
                    pool.effectCreated = NULL;
                    if (wor_impactVisual(&pool, 7, m->x, m->y) == WOR_ERROR) return;
                }
                else if (!m->target)
                    wmv_damageTerrain(context, m->x >= 0 ? m->x / 16 : -((15 - m->x) / 16),
                                      m->y >= 0 ? m->y / 16 : -((15 - m->y) / 16), m->damage);
                else wft_strike(context, shooter, target, m->damage);
                m->used = false;
                break;
            }
            default:
                m->used = false;
                break;
        }
    }
}

// One tick of an attacking unit's attack script: its blow or its shot at
// the action.
static void wft_action(WarContext* context, WarEntity* entity)
{
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    WarEntity* target = we_getAttackTarget(context, entity);
    const s32 mtype = wft_missileType(unit->type);
    if (mtype == 12)
    {
        // (0x106EC: a type-23 unit's blow shows a strike first, 0x11EC0)
        WarOriginalUnit* self = wos_unit(context, entity);
        if (self && self->type == 23) wft_poolVisual(context, 7, entity);
        const s32 damage = wft_roll(context, unit, target);
        if (target) wft_strike(context, entity, target, damage);
        else
        {
            WarOriginalUnit* original = wos_unit(context, entity);
            if (original && original->current == 7)
                wmv_damageTerrain(context, original->goalX, original->goalY, (u8)damage);
        }
    }
    else if (wos_shotMana(context, entity, true) != WOR_ERROR)
        wft_launch(context, entity, unit, target, mtype);
}

void wft_unitTick(WarContext* context, WarEntity* e, WarStateBase* st)
{
    WarUnitComponent* u = we_getUnitComponent(context, e);
    if (!u) return;
    if (st && st->type == WAR_STATE_CHOPPING) return;   // (its strokes: wst_frankChopTick)
    if (!st || st->type != WAR_STATE_ATTACK || u->wmvStepping)
    {
        u->frankAtkOn = false;
        return;
    }
    if (u->frankScriptChanged) return; // no second script in the same unit turn (0x2ECFC)
    if (!u->frankAtkOn)
    {
#ifndef FRANK_BOARD
        if (getenv("WAR_FIGHTDEBUG")) fprintf(stderr, "attack script starts t %.3f tick %u by %d (type %d)\n", context->gameTime, wrng_tick, (s32)e->id, u->type);
#endif
        if (!wos_select(context, e, ((WarStateAttack*)st)->frankOrder, 1)) return;
        u->frankAtkOn = true;
    }
#ifndef FRANK_BOARD
    {
        static s32 watch = -2;
        if (watch == -2) watch = getenv("WAR_ATKDEBUG") ? atoi(getenv("WAR_ATKDEBUG")) : -1;
        WarOriginalUnit* original = wos_unit(context, e);
        if ((s32)e->id == watch && original) fprintf(stderr, "atk tick %u pc %u wait %u target %d\n", wrng_tick, original->pc, original->wait, (s32)((WarStateAttack*)st)->targetEntityId);
    }
#endif
    if (wos_tick(context, e, wft_action) != WOR_CHANGED) return;
    if (wmv_takePendingOrder(context, e)) return;
    if (((WarStateAttack*)st)->frankOrder == 7)
    {
        u->frankAtkCheck = true;
        return;
    }
    // The action-target check at the attack checkpoint (0x2EFEC).
    const WarEntityId targetId = ((WarStateAttack*)st)->targetEntityId;
    WarEntity* target = targetId ? we_findEntity(context, targetId) : NULL;
    WarUnitComponent* tu = target && wu_isUnit(target) ? we_getUnitComponent(context, target) : NULL;
    if (!target || (wu_isUnit(target) &&
        (!tu || tu->hp <= 0 || wst_isDead(context, target) || wst_isCollapsing(context, target) ||
         wst_isInsideBuilding(context, target))))
    {
        u->frankAtkOn = false;
        wst_popState(context, e, WAR_TRANSITION_CAUSE_COMPLETION, WAR_STATE_RESULT_SUCCESS);
        wmv_settle(context, e);
    }
    else u->frankAtkCheck = true;
}

void wft_tickEnd(WarContext* context)
{
    wft_missilesTick(context);
}
