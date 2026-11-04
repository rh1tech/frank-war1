// frank-war1 — the original WarCraft computer player, reimplemented on
// war1's state machines (included by war_ai.c in the frank build, in place
// of its placeholder AI). Behaviour and numbers as described in docs/ai.md;
// every parameter comes from the level's header in DATA.WAR at run time
// (WarLevelAI, war_resources.c).
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib

// One AI tick of the original at normal speed (33.3 ticks a second); the
// AI here runs every WAI_STEP ticks of game time.
#define WAI_TICK_SECONDS (1.0f / 33.333f)
#define WAI_STEP 3

enum
{
    WAI_ROLE_NONE   = 0,
    WAI_ROLE_ATTACK = 4,   // go for the target zone
    WAI_ROLE_GUARD  = 8,   // hold this spot, react close by
    WAI_ROLE_ROUTE  = 16,  // walking an attack route
    WAI_ROLE_POST   = 32,  // holds a starting post
};

typedef struct
{
    WarEntityId id;        // which entity this slot describes (slots are reused)
    u8 role;
    u8 waiting;            // counted at its route waypoint
    u8 gather;             // its waypoint is the route's gathering point
    s16 route;             // route table word index of its waypoint
    s16 postX, postY;      // its post (tiles)
    s32 timer;             // buildings: production countdown (ticks)
    s16 lastX, lastY;      // route units: where it was, for noticing it is stuck
    s32 still;             // ticks without moving
    s8 thinkWait;          // AI steps to its next think while idle
    WarEntityId attacker;  // the last unit to attack it (the original's +0x3A)
    f32 retargetAt;        // game time of its next look round while attacking
} WarAIUnit;

typedef struct
{
    WarUnitType type;
    u8 player;
    s16 x, y;
    WarEntityId id;        // 0: destroyed (a placeholder to rebuild)
} WarAIPlan;

typedef struct
{
    WarUnitType type;
    u8 player;
    s16 x, y;
    WarEntityId id;        // the unit holding it; 0: free for a new one
} WarAIPost;

#define WAI_PLAN_MAX 64
#define WAI_POST_MAX 96

typedef struct
{
    WarLevelAI* lv;
    u64 rng;
    f64 nextStep;
    bool started;          // the level's own units have been given their roles
    s32 reaim;             // ticks to the next re-aim
    s32 wave;              // ticks to the next wave (< 0: none)
    s32 targetX, targetY;  // attack target, tiles (-1: none yet)
    s32 quota[MAX_PLAYERS_COUNT];
    s32 groupSize, groupsLaunched;
    WarAIUnit units[MAX_ENTITIES_COUNT];
    WarAIPlan plan[WAI_PLAN_MAX];
    s32 planCount;
    WarAIPost posts[WAI_POST_MAX];
    s32 postCount;
    u16 routes[WAR_AI_ROUTE_WORDS_MAX];
    s32 routeCount;
    s32 homeX[MAX_PLAYERS_COUNT], homeY[MAX_PLAYERS_COUNT];
    s32 casters[MAX_PLAYERS_COUNT], workers[MAX_PLAYERS_COUNT], farms[MAX_PLAYERS_COUNT];
    s32 builders[MAX_PLAYERS_COUNT], goldWorkers[MAX_PLAYERS_COUNT];
    bool hasMill, hasSmith;   // owned by any computer player
    bool mill0, smith0;       // owned by the human (autoplay)
    bool heroJoined;          // Lothar or Garona has been the human's
    bool ticksSeen;           // (host research log)
    s32 targetX0, targetY0;   // autoplay: the human side's attack target
    s32 wave0, waves0;        // autoplay: ticks to its next check, waves sent
} WarAIState;

static WarAIState* wai;
static s32 wai_self;          // the player whose unit is thinking

// Host test mode: the AI also plays the human (player 0), paying for what
// it trains, to play the campaign AI vs AI.
bool wai_autoHuman;
bool wai_autoHumanFree;   // ... and, like the computer, trains for free

// ------------------------------------------------------------------ helpers

static s32 wai_rand(s32 n)
{
    return SDL_rand_r(&wai->rng, n);
}

static bool wai_isComputer(WarContext* context, s32 player)
{
    NOT_USED(context);
    if (wai && wai->lv && player == 0 && wai_autoHuman) return true;
    return wai && wai->lv && player > 0 && player < MAX_PLAYERS_COUNT && wai->lv->alliance[player] == 1;
}

// For war1's engine (mana rates): a computer player of the level
bool wai_isComputerPlayer(WarContext* context, s32 player)
{
    return wai && wai->lv && player > 0 && wai_isComputer(context, player);
}

// war1's idle state leaves the units of computer players to the AI
bool wai_ownsIdle(WarContext* context, WarEntity* entity)
{
    WarUnitComponent* u = we_getUnitComponent(context, entity);
    if (!u || !wai || !wai->lv) return false;
    // the human's units too: their idle behaviour is the original's as well
    return u->player == 0 || wai_isComputer(context, u->player);
}

static WarAIUnit* wai_unitInfo(WarContext* context, WarEntity* entity);

// war1's attack code reports each blow (we_meleeAttack, arrows included)
void wai_noteAttacker(WarContext* context, WarEntity* target, WarEntity* attacker)
{
    if (!wai || !wai->lv || !target || !attacker) return;
    WarAIUnit* info = wai_unitInfo(context, target);
    if (info && info->id == target->id) info->attacker = attacker->id;
}

static bool wai_isEnemy(s32 self, s32 other)
{
    if (self != 0) return other == 0;
    return other > 0 && other < MAX_PLAYERS_COUNT && wai->lv->alliance[other] == 1;
}

static WarAIUnit* wai_unitInfo(WarContext* context, WarEntity* entity)
{
    WarEntityManager* manager = we_getEntityManager(context);
    s32 slot = (s32)(entity - manager->entities);
    if (slot < 0 || slot >= MAX_ENTITIES_COUNT)
        return NULL;
    return &wai->units[slot];
}

static bool wai_alive(WarContext* context, WarEntity* entity)
{
    return entity && wu_isUnit(entity) && !wst_isDead(context, entity) && !wst_isCollapsing(context, entity);
}

// war1 has no stats (speed, damage) for a few placed-only types: leave
// those to war1's own idle behaviour
static bool wai_hasStats(WarUnitType t)
{
    for (s32 i = 0; i < (s32)arrayLength(unitStats); i++)
        if (unitStats[i].type == t) return true;
    return false;
}

static bool wai_isWorkerType(WarUnitType t) { return t == WAR_UNIT_PEASANT || t == WAR_UNIT_PEON; }
static bool wai_isCasterType(WarUnitType t) { return t >= WAR_UNIT_CONJURER && t <= WAR_UNIT_MEDIVH; }
static bool wai_isHeroType(WarUnitType t) { return t >= WAR_UNIT_LOTHAR && t <= WAR_UNIT_GARONA; }
static bool wai_isBuildingType(WarUnitType t) { return t >= WAR_UNIT_FARM_HUMANS && t <= WAR_UNIT_GOLDMINE; }
static bool wai_isMonsterType(WarUnitType t) { return t >= WAR_UNIT_OGRE && t <= WAR_UNIT_30; }
static bool wai_isHallType(WarUnitType t)
{
    return t == WAR_UNIT_TOWNHALL_HUMANS || t == WAR_UNIT_TOWNHALL_ORCS ||
           t == WAR_UNIT_STORMWIND || t == WAR_UNIT_BLACKROCK;
}

static s32 wai_tileDistance(s32 x0, s32 y0, s32 x1, s32 y1)
{
    s32 dx = ABS(x0 - x1), dy = ABS(y0 - y1);
    return MAX(dx, dy);
}

// war1 keeps tilex/tiley for buildings only: a unit's tile from its position
static void wai_tile(WarContext* context, WarEntity* e, s32* x, s32* y)
{
    vec2 t = wmap_mapToTileCoordinatesV(wu_getUnitCenterPosition(context, e));
    *x = (s32)t.x;
    *y = (s32)t.y;
}

static vec2 wai_tileCenter(s32 x, s32 y)
{
    return wmap_tileToMapCoordinatesV(vec2i(x, y), true);
}

// The original's target priorities (docs/ai.md "What attackers and guards do").
// The original's tables (WAR.EXE 1.20 data): target priority by unit type
// (0x51D28) and attack range by unit type (0x51C6C). war1's unit types are
// the original's numbers.
static const u8 wai_priorityTable[51] = {
    40, 40, 60, 60, 105, 105, 45, 45, 40, 40, 55, 55, 50, 50, 1, 1, 1, 1, 1, 41, 41, 35, 41, 41, 35, 41,
    30, 41, 70, 41, 41, 70, 20, 20, 18, 18, 18, 18, 18, 18, 22, 22, 8, 8, 12, 12, 10, 10, 25, 25, 0 };
static const u8 wai_rangeTable[32] = {
    1, 1, 1, 1, 8, 8, 1, 1, 5, 4, 3, 2, 1, 2, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3 };

static s32 wai_targetPriority(WarUnitType t)
{
    s32 o = wu_toOriginalType(t);
    return (u32)o < 51 ? wai_priorityTable[o] : 0;
}

static s32 wai_attackRange(WarUnitType t)
{
    s32 o = wu_toOriginalType(t);
    return (u32)o < 32 ? wai_rangeTable[o] : 1;
}

// The best human-side target within `radius` tiles of (x, y), by priority.
// The step's targets, gathered once (wai_gatherTargets): every idle unit
// looks for one, and checking each unit's state for each of them cost the
// board a fifth of its time on the big maps.
typedef struct
{
    WarEntity* e;
    s16 x, y, w, h;   // footprint in tiles
    u8 player;
    u8 priority;
    bool building;
} WarAITarget;

static WarAITarget wai_targets[MAX_ENTITIES_COUNT];
static s32 wai_targetCount;

// The step's candidates, in the entity pool's slot order (the original
// walks its unit table in slot order, and its choice depends on that
// order); gathered once a step.
static void wai_gatherTargets(WarContext* context)
{
    wai_targetCount = 0;
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 i = 1; i < MAX_ENTITIES_COUNT && wai_targetCount < MAX_ENTITIES_COUNT; i++)
    {
        WarEntity* e = &manager->entities[i];
        if (!e->id || !wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || u->player >= MAX_PLAYERS_COUNT || u->invisible || u->type >= WAR_UNIT_HUMAN_CORPSE) continue;
        // workers inside a mine or a hall are out of reach
        if (wai_isWorkerType(u->type) && (wst_isMining(context, e) || !we_isComponentEnabled(context, e, COMP_SPRITE))) continue;
        s32 x, y;
        wai_tile(context, e, &x, &y);
        WarAITarget t = { e, (s16)x, (s16)y, 1, 1, u->player, (u8)wai_targetPriority(u->type), wai_isBuildingType(u->type) };
        if (t.building) { t.x = (s16)u->tilex; t.y = (s16)u->tiley; t.w = (s16)u->sizex; t.h = (s16)u->sizey; }
        wai_targets[wai_targetCount++] = t;
    }
}

// The original's distance (0x13270): Chebyshev, to the nearest tile of a
// building's footprint.
static s32 wai_targetDistance(const WarAITarget* t, s32 x, s32 y)
{
    s32 nx = CLAMP(x, t->x, t->x + t->w - 1), ny = CLAMP(y, t->y, t->y + t->h - 1);
    return MAX(ABS(nx - x), ABS(ny - y));
}

// The original's target choice (0x2A0DC): among the hostile units within
// `radius` of (x, y), in slot order, the highest priority; a unit beats a
// building held so far, and one within `range` beats one held outside it.
static WarEntity* wai_findTargetEx(WarContext* context, s32 x, s32 y, s32 radius, s32 range)
{
    NOT_USED(context);
    const WarAITarget* best = NULL;
    s32 bestPriority = 0, bestDistance = 0xFFFF;
    for (s32 i = 0; i < wai_targetCount; i++)
    {
        const WarAITarget* t = &wai_targets[i];
        if (!wai_isEnemy(wai_self, t->player)) continue;
        s32 d = wai_targetDistance(t, x, y);
        if (d > radius) continue;
        if (best && best->building && !t->building) bestPriority = 0;
        else if (bestDistance > range && d <= range) bestPriority = 0;
        if (bestPriority < t->priority)
        {
            best = t;
            bestPriority = t->priority;
            bestDistance = d;
        }
    }
    return best ? best->e : NULL;
}

static WarEntity* wai_findTarget(WarContext* context, s32 x, s32 y, s32 radius)
{
    return wai_findTargetEx(context, x, y, radius, 1);
}

static bool wai_order(WarContext* context, WarEntity* entity, WarStateBase* state)
{
    if (!state) return false;
    return wst_resetState(context, entity, state, WAR_TRANSITION_CAUSE_AI_ORDER);
}

// The original's "attack" to a place (0x21020: orders 3 then 6): a plain
// walk (its move order looks for nobody on the way, 0x551A4), then, idle at
// the place, the unit's think finds its targets.
static bool wai_attackMove(WarContext* context, WarEntity* entity, s32 x, s32 y)
{
    if (!wst_canSubmitTransition(context, entity, WAR_INTERRUPT_AI_ORDER)) return false;
    vec2 from = wu_getUnitCenterPosition(context, entity);
    return wai_order(context, entity, (WarStateBase*)wst_createMoveState(context, entity, 2, arrayArg(vec2, from, wai_tileCenter(x, y)), false));
}

static bool wai_attack(WarContext* context, WarEntity* entity, WarEntity* target)
{
    if (!wst_canSubmitTransition(context, entity, WAR_INTERRUPT_AI_ORDER)) return false;
    if (!wu_canAttack(context, entity, target)) return false;
    return wai_order(context, entity, (WarStateBase*)wst_createAttackState(
        context, entity, target->id, wu_getUnitCenterPosition(context, target)));
}

static WarEntity* wai_townHall(WarContext* context, s32 player)
{
    for (WarUnitType t = WAR_UNIT_TOWNHALL_HUMANS; t <= WAR_UNIT_BLACKROCK; t++)
    {
        if (!wai_isHallType(t)) continue;
        WarEntityList* list = we_getUnitsOfType(context, t);
        for (s32 i = 0; i < list->count; i++)
        {
            WarEntity* e = list->items[i];
            WarUnitComponent* u = wai_alive(context, e) ? we_getUnitComponent(context, e) : NULL;
            if (u && u->player == player && !u->building) return e;
        }
    }
    return NULL;
}

static WarRace wai_race(WarContext* context, s32 player)
{
    return context->map->players[player].race;
}

// ------------------------------------------------------------- the clock

static void wai_reaimFor(WarContext* context, s32 self, s32* tx, s32* ty)
{
    // 4x4 zones of 16x16 tiles, scored by the enemy's presence
    s32 score[16] = {0};
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        if (!wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || !wai_isEnemy(self, u->player) || u->type >= WAR_UNIT_HUMAN_CORPSE) continue;
        s32 ux, uy;
        wai_tile(context, e, &ux, &uy);
        s32 z = CLAMP(uy / 16, 0, 3) * 4 + CLAMP(ux / 16, 0, 3);
        score[z] += wai_isBuildingType(u->type) ? 4 : 1;
    }
    s32 best = -1, bestScore = 0;
    for (s32 z = 0; z < 16; z++)
    {
        if (score[z] > bestScore) { bestScore = score[z]; best = z; }
    }
    if (best >= 0)
    {
        *tx = (best % 4) * 16 + 8;
        *ty = (best / 4) * 16 + 8;
    }
}

static void wai_reaim(WarContext* context)
{
    wai_reaimFor(context, 1, &wai->targetX, &wai->targetY);
    if (wai_autoHuman) wai_reaimFor(context, 0, &wai->targetX0, &wai->targetY0);
}

static bool wai_target(s32 player, s32* x, s32* y)
{
    *x = player == 0 ? wai->targetX0 : wai->targetX;
    *y = player == 0 ? wai->targetY0 : wai->targetY;
    return *x >= 0;
}

// All of a player's mobile units (not workers or heroes) attack.
static void wai_launchWave(WarContext* context)
{
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        if (!wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || !wai_isComputer(context, u->player) || wai->quota[u->player] <= 0) continue;
        if (wai_isBuildingType(u->type) || wai_isWorkerType(u->type) || wai_isHeroType(u->type) || !wai_hasStats(u->type)) continue;
        WarAIUnit* info = wai_unitInfo(context, e);
        if (!info || info->id != e->id) continue;
        if (info->waiting && info->route >= 0 && wai->routes[info->route] > 0)
            wai->routes[info->route]--;
        info->role = WAI_ROLE_ATTACK;
        info->waiting = 0;
        wai->quota[u->player]--;
    }
    for (s32 p = 0; p < MAX_PLAYERS_COUNT; p++) wai->quota[p] = 0;
}

static void wai_allOut(WarContext* context, s32 player)
{
#ifdef WAR_MEMDEBUG
    if (wai->quota[player] != 0xFFFF) printf("ai: all-out player %d at %.0f s\n", player, context->gameTime);
#endif
    wai->quota[player] = 0xFFFF;
    wai->wave = 1;
}

// ------------------------------------------------------------- production

static void wai_train(WarContext* context, WarEntity* building, WarUnitType type)
{
    const WarUnitStats* stats = wu_getUnitStats(type);
    if (!stats || !wst_canSubmitTransition(context, building, WAR_INTERRUPT_AI_ORDER)) return;
    WarUnitComponent* bu = we_getUnitComponent(context, building);
    if (bu->player == 0 && !wai_autoHumanFree)
    {
        // autoplay: the human side pays and needs food, like the player
        WarPlayerInfo* pi = &context->map->players[0];
        if (!we_enoughFarmFood(context, pi) || !we_enoughPlayerResources(context, pi, stats->goldCost, stats->woodCost)) return;
        wai_order(context, building, (WarStateBase*)wst_createTrainState(
            context, building, type, wu_unitTrainSeconds(stats), stats->goldCost, stats->woodCost, NULL));
        return;
    }
    // free: the computer never pays for units
    wai_order(context, building, (WarStateBase*)wst_createTrainState(
        context, building, type, wu_unitTrainSeconds(stats), 0, 0, NULL));
}

static void wai_research(WarContext* context, WarEntity* building, s32 player, s32 group)
{
    WarPlayerInfo* pi = &context->map->players[player];
    if (pi->upgrades[group].level >= 2) return;
    WarUpgradeType type = group == 9 ? WAR_UPGRADE_SHIELD
        : (WarUpgradeType)(group * 2 + (wai_race(context, player) == WAR_RACE_ORCS ? 1 : 0));
    const WarUpgradeStats* stats = wu_getUpgradeStats(type);
    if (!stats || !wst_canSubmitTransition(context, building, WAR_INTERRUPT_AI_ORDER)) return;
    s32 cost = 0;
    if (player == 0)
    {
        if (pi->upgrades[group].level >= pi->upgrades[group].allowed) return;
        if (wai_autoHumanFree) goto order;
        cost = stats->goldCost[pi->upgrades[group].level];
        if (!we_enoughPlayerResources(context, pi, cost, 0)) return;
    }
order:
    wai_order(context, building, (WarStateBase*)wst_createUpgradeState(
        context, building, type, (f32)stats->buildTime, cost, 0));
}

static void wai_produce(WarContext* context, WarEntity* building, WarUnitComponent* u)
{
    s32 player = u->player;
    bool orc = (u->type - WAR_UNIT_FARM_HUMANS) & 1;
    WarResource* levelInfo = wres_getOrCreateResource(context, context->map->levelInfoIndex);
    const u8* allowed = levelInfo->levelInfo.allowedFeatures;

    switch (u->type)
    {
        case WAR_UNIT_BARRACKS_HUMANS:
        case WAR_UNIT_BARRACKS_ORCS:
        {
            // weights: soldier 4, archer 6, catapult 2, knight 8 (allowed ones;
            // archers need a lumber mill, catapults and knights a blacksmith)
            s32 w[4];
            w[0] = allowed[0] ? 4 : 0;
            bool mill = player == 0 ? wai->mill0 : wai->hasMill;
            bool smith = player == 0 ? wai->smith0 : wai->hasSmith;
            w[1] = allowed[4] && mill ? 6 : 0;
            w[2] = allowed[2] && smith ? 2 : 0;
            w[3] = allowed[3] && smith ? 8 : 0;
            s32 sum = w[0] + w[1] + w[2] + w[3];
            if (sum == 0) return;
            s32 r = wai_rand(sum), k = 0;
            while (r >= w[k]) r -= w[k++];
            static const WarUnitType human[4] = { WAR_UNIT_FOOTMAN, WAR_UNIT_ARCHER, WAR_UNIT_CATAPULT_HUMANS, WAR_UNIT_KNIGHT };
            static const WarUnitType orcs[4] = { WAR_UNIT_GRUNT, WAR_UNIT_SPEARMAN, WAR_UNIT_CATAPULT_ORCS, WAR_UNIT_RAIDER };
            wai_train(context, building, orc ? orcs[k] : human[k]);
            break;
        }
        case WAR_UNIT_CHURCH:
        case WAR_UNIT_TEMPLE:
            if (wai->casters[player] <= MIN(wai->farms[player], 4))
                wai_train(context, building, orc ? WAR_UNIT_NECROLYTE : WAR_UNIT_CLERIC);
            break;
        case WAR_UNIT_TOWER_HUMANS:
        case WAR_UNIT_TOWER_ORCS:
            if (wai->casters[player] <= MIN(wai->farms[player], 4))
                wai_train(context, building, orc ? WAR_UNIT_WARLOCK : WAR_UNIT_CONJURER);
            break;
        case WAR_UNIT_TOWNHALL_HUMANS:
        case WAR_UNIT_TOWNHALL_ORCS:
        case WAR_UNIT_STORMWIND:
        case WAR_UNIT_BLACKROCK:
            if (wai->workers[player] < (player == 0 ? MAX(wai->lv->workerLimit[0], 6) : wai->lv->workerLimit[player]))
                wai_train(context, building, orc ? WAR_UNIT_PEON : WAR_UNIT_PEASANT);
            break;
        case WAR_UNIT_LUMBERMILL_HUMANS:
        case WAR_UNIT_LUMBERMILL_ORCS:
            wai_research(context, building, player, 0);
            break;
        case WAR_UNIT_STABLE:
        case WAR_UNIT_KENNEL:
            wai_research(context, building, player, 2);
            break;
        case WAR_UNIT_BLACKSMITH_HUMANS:
        case WAR_UNIT_BLACKSMITH_ORCS:
        {
            WarPlayerInfo* pi = &context->map->players[player];
            wai_research(context, building, player, pi->upgrades[9].level < pi->upgrades[1].level ? 9 : 1);
            break;
        }
        default:
            break;
    }
}

// ------------------------------------------------------------------ town plan

static s32 wai_rebuildPriority(WarUnitType t)
{
    if (t == WAR_UNIT_TOWNHALL_HUMANS || t == WAR_UNIT_TOWNHALL_ORCS) return 120;
    if (t == WAR_UNIT_FARM_HUMANS || t == WAR_UNIT_FARM_ORCS) return 50;
    if (t >= WAR_UNIT_BARRACKS_HUMANS && t <= WAR_UNIT_TOWER_ORCS) return 40;
    if (t >= WAR_UNIT_LUMBERMILL_HUMANS && t <= WAR_UNIT_BLACKSMITH_ORCS) return 20;
    return 0; // the keeps are never rebuilt
}

static s32 wai_repairPriority(WarUnitType t)
{
    if (t == WAR_UNIT_STORMWIND || t == WAR_UNIT_BLACKROCK) return 50;
    if (wai_isHallType(t) || t == WAR_UNIT_FARM_HUMANS || t == WAR_UNIT_FARM_ORCS) return 45;
    if (t >= WAR_UNIT_BARRACKS_HUMANS && t <= WAR_UNIT_TOWER_ORCS) return 35;
    if (t >= WAR_UNIT_LUMBERMILL_HUMANS && t <= WAR_UNIT_BLACKSMITH_ORCS) return 15;
    return 0;
}

static bool wai_siteClear(WarContext* context, WarUnitType type, s32 x, s32 y)
{
    const WarUnitData* data = wu_getUnitData(type);
    for (s32 j = 0; j < data->sizey; j++)
        for (s32 i = 0; i < data->sizex; i++)
            if (!wpath_isEmpty(&context->map->finder, x + i, y + j)) return false;
    return true;
}

// A worker with nothing to carry looks for a placeholder to rebuild or a
// damaged building to repair, highest priority first.
static bool wai_townPlanWork(WarContext* context, WarEntity* worker, s32 player)
{
    WarPlayerInfo* pi = &context->map->players[player];
    bool poor = pi->gold < 200 && pi->wood < 100;
    s32 bestPriority = 0;
    WarAIPlan* bestPlan = NULL;
    WarEntity* bestRepair = NULL;

    for (s32 i = 0; i < wai->planCount; i++)
    {
        WarAIPlan* plan = &wai->plan[i];
        if (plan->player != player) continue;
        if (poor && !wai_isHallType(plan->type)) continue;
        WarEntity* e = plan->id ? we_findEntity(context, plan->id) : NULL;
        if (!e || !wai_alive(context, e))
        {
            plan->id = 0;
            s32 p = wai_rebuildPriority(plan->type);
            if (p > bestPriority) { bestPriority = p; bestPlan = plan; bestRepair = NULL; }
            continue;
        }
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (u && !u->building && u->hp < u->maxhp * 3 / 4)
        {
            s32 p = wai_repairPriority(u->type);
            if (p > bestPriority) { bestPriority = p; bestPlan = NULL; bestRepair = e; }
        }
    }

    if (bestRepair)
        return wai_order(context, worker, (WarStateBase*)wst_createRepairState(context, worker, bestRepair->id));

    if (!bestPlan || !wai_siteClear(context, bestPlan->type, bestPlan->x, bestPlan->y))
        return false;

    // rebuilding is paid (the town hall excepted)
    const WarBuildingStats* stats = wu_getBuildingStats(bestPlan->type);
    s32 gold = 0, wood = 0;
    if (!wai_isHallType(bestPlan->type) && stats)
    {
        gold = stats->goldCost;
        wood = stats->woodCost;
        if (!we_enoughPlayerResources(context, pi, gold, wood)) return false;
    }
    if (!wst_canSubmitTransition(context, worker, WAR_INTERRUPT_AI_ORDER)) return false;
    WarEntity* building = we_createBuilding(context, CREATE_UNIT_ARGS_INIT(
        .type = bestPlan->type, .x = bestPlan->x, .y = bestPlan->y, .player = (u8)player, .isGoingToBuild = true));
    if (!building) return false;
    if (gold || wood) we_decreasePlayerResources(context, pi, gold, wood);
    bestPlan->id = building->id;
    return wai_order(context, worker, (WarStateBase*)wst_createRepairState(context, worker, building->id));
}

// Autoplay: the human side founds buildings as a player would (the
// computer never does): farms for food, then barracks, mill, blacksmith.
static bool wai_findSite(WarContext* context, WarUnitType type, s32 player, s32* sx, s32* sy)
{
    const WarUnitData* data = wu_getUnitData(type);
    s32 hx = wai->homeX[player], hy = wai->homeY[player];
    WarEntityList* mines = we_getUnitsOfType(context, WAR_UNIT_GOLDMINE);
    for (s32 r = 3; r <= 16; r++)
    {
        for (s32 k = 0; k < 8 * r; k++)
        {
            // walk the ring of radius r
            s32 side = k / (2 * r), o = k % (2 * r) - r;
            s32 x = side == 0 ? hx + o : side == 1 ? hx + r : side == 2 ? hx - o : hx - r;
            s32 y = side == 0 ? hy - r : side == 1 ? hy + o : side == 2 ? hy + r : hy - o;
            if (x < 1 || y < 1 || x + data->sizex >= MAP_TILES_WIDTH - 1 || y + data->sizey >= MAP_TILES_HEIGHT - 1) continue;
            // a free tile all around, to keep paths open
            if (!we_checkRectToBuild(context, x - 1, y - 1, data->sizex + 2, data->sizey + 2)) continue;
            bool nearMine = false;
            for (s32 i = 0; i < mines->count && !nearMine; i++)
            {
                WarUnitComponent* mu = we_getUnitComponent(context, mines->items[i]);
                if (mu && wai_tileDistance(x, y, mu->tilex, mu->tiley) <= 4) nearMine = true;
            }
            if (nearMine) continue;
            *sx = x; *sy = y;
            return true;
        }
    }
    return false;
}

static bool wai_humanBuild(WarContext* context, WarEntity* worker, s32 player)
{
    WarPlayerInfo* pi = &context->map->players[player];
    WarResource* levelInfo = wres_getOrCreateResource(context, context->map->levelInfoIndex);
    const u8* allowed = levelInfo->levelInfo.allowedFeatures;
    s32 count[WAR_UNIT_COUNT] = {0};
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        WarUnitComponent* u = wai_alive(context, e) ? we_getUnitComponent(context, e) : NULL;
        if (!u || u->player != player || !wai_isBuildingType(u->type)) continue;
        if (u->building) return false;   // one at a time
        count[u->type]++;
    }
    WarRace race = pi->race;
    WarUnitType farm = wu_getUnitTypeForRace(WAR_UNIT_FARM_HUMANS, race);
    WarUnitType barracks = wu_getUnitTypeForRace(WAR_UNIT_BARRACKS_HUMANS, race);
    WarUnitType mill = wu_getUnitTypeForRace(WAR_UNIT_LUMBERMILL_HUMANS, race);
    WarUnitType smith = wu_getUnitTypeForRace(WAR_UNIT_BLACKSMITH_HUMANS, race);
    s32 food = count[farm] * 4 + 1, dudes = wu_getTotalNumberOfDudes(context, player);

    WarUnitType type = WAR_UNIT_COUNT;
    if (allowed[7] && dudes + 2 >= food) type = farm;
    else if (allowed[8] && count[barracks] == 0) type = barracks;
    else if (allowed[12] && count[mill] == 0) type = mill;
    else if (allowed[14] && count[smith] == 0) type = smith;
    else if (allowed[7] && dudes + 5 >= food && count[farm] < 10) type = farm;
    else if (allowed[8] && count[barracks] < 2 && pi->gold >= 2000) type = barracks;
    if (type == WAR_UNIT_COUNT) return false;

    const WarBuildingStats* stats = wu_getBuildingStats(type);
    if (!stats || !we_enoughPlayerResources(context, pi, stats->goldCost, stats->woodCost)) return false;
    s32 x, y;
    if (!wai_findSite(context, type, player, &x, &y)) return false;
    if (!wst_canSubmitTransition(context, worker, WAR_INTERRUPT_AI_ORDER)) return false;
    WarEntity* building = we_createBuilding(context, CREATE_UNIT_ARGS_INIT(
        .type = type, .x = x, .y = y, .player = (u8)player, .isGoingToBuild = true));
    if (!building) return false;
    we_decreasePlayerResources(context, pi, stats->goldCost, stats->woodCost);
    if (wai->planCount < WAI_PLAN_MAX)
        wai->plan[wai->planCount++] = (WarAIPlan){ type, (u8)player, (s16)x, (s16)y, building->id };
    return wai_order(context, worker, (WarStateBase*)wst_createRepairState(context, worker, building->id));
}

// ------------------------------------------------------------------ workers

static bool wai_mineGold(WarContext* context, WarEntity* worker, WarUnitComponent* u)
{
    WarEntity* best = NULL;
    s32 bestCost = INT32_MAX;
    s32 wx, wy;
    wai_tile(context, worker, &wx, &wy);
    WarEntityList* mines = we_getUnitsOfType(context, WAR_UNIT_GOLDMINE);
    for (s32 i = 0; i < mines->count; i++)
    {
        WarEntity* m = mines->items[i];
        WarUnitComponent* mu = wai_alive(context, m) ? we_getUnitComponent(context, m) : NULL;
        if (!mu || mu->amount <= 0) continue;
        s32 cost = wai_tileDistance(wai->homeX[u->player], wai->homeY[u->player], mu->tilex, mu->tiley) +
                   wai_tileDistance(wx, wy, mu->tilex, mu->tiley);
        if (cost < bestCost) { bestCost = cost; best = m; }
    }
    if (!best) return false;
    return wai_order(context, worker, (WarStateBase*)wst_createGatherGoldState(context, worker, best->id));
}

// The nearest tree with wood that can be reached, from the forests' own
// lists: the computer does not care about the fog (war1's own search only
// takes explored trees, and flood-fills the whole map for a computer worker).
static bool wai_chopWood(WarContext* context, WarEntity* worker, WarUnitComponent* u)
{
    s32 hx = wai->homeX[u->player], hy = wai->homeY[u->player];
    s32 wx, wy;
    wai_tile(context, worker, &wx, &wy);
    WarEntity* bestForest = NULL;
    WarTree* bestTree = NULL;
    s32 bestCost = INT32_MAX;
    WarEntityList* forests = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_FOREST);
    for (s32 i = 0; i < forests->count; i++)
    {
        WarEntity* forest = forests->items[i];
        WarForestComponent* fc = forest ? we_getForestComponent(context, forest) : NULL;
        if (!fc) continue;
        for (s32 k = 0; k < fc->trees.count; k++)
        {
            WarTree* tree = &fc->trees.items[k];
            if (tree->amount <= 0) continue;
            s32 cost = 2 * wai_tileDistance(hx, hy, tree->tilex, tree->tiley) + wai_tileDistance(wx, wy, tree->tilex, tree->tiley);
            if (cost >= bestCost) continue;
            if (!wpath_isTileAccesible(&context->map->finder, tree->tilex, tree->tiley)) continue;
            bestCost = cost;
            bestForest = forest;
            bestTree = tree;
        }
    }
    if (!bestTree) return false;
    return wai_order(context, worker, (WarStateBase*)wst_createGatherWoodState(
        context, worker, bestForest->id, wai_tileCenter(bestTree->tilex, bestTree->tiley)));
}

static void wai_leaveHallSite(WarContext* context, WarEntity* e, WarUnitComponent* u);

// The original's worker think (0x2B154): deliver a load; else, while
// builders < 2/3 of the other workers, town-plan work; else gold while the
// miners are fewer than the other workers (or when attacked); else wood;
// else step off a lost hall's spot.
static void wai_workerThink(WarContext* context, WarEntity* worker, WarUnitComponent* u, WarAIUnit* info)
{
    s32 player = u->player;
    if (wu_isCarryingResources(context, worker))
    {
        WarEntity* hall = wai_townHall(context, player);
        if (hall && wai_order(context, worker, (WarStateBase*)wst_createDeliverState(context, worker, hall->id)))
            return;
        // no hall to deliver to: only town-plan work
        if (wai_townPlanWork(context, worker, player)) wai->builders[player]++;
        return;
    }
    s32 target = 2 * (wai->workers[player] - 1) / 3;
    if (player == 0 && wai->builders[player] < target && wai_humanBuild(context, worker, player))
    {
        wai->builders[player]++;
        return;
    }
    if (wai->builders[player] < target && wai_townPlanWork(context, worker, player))
    {
        wai->builders[player]++;
        return;
    }
    if ((info->attacker || wai->goldWorkers[player] < wai->workers[player] - 1) && wai_mineGold(context, worker, u))
    {
        wai->goldWorkers[player]++;
        return;
    }
    if (info->attacker) return;
    if (wai_chopWood(context, worker, u)) return;
    wai_leaveHallSite(context, worker, u);
}

// ------------------------------------------------------------- soldiers

// On to the next waypoint; at the route's end the unit becomes an attacker.
// Casters stop one waypoint short of the end and stay there.
static void wai_routeAdvance(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info)
{
    s32 next = info->route + 3;
    info->waiting = 0;
    info->gather = 0;
    info->still = 0;
    if (next + 2 >= wai->routeCount || wai->routes[next] == 0xFFFF)
    {
        info->role = WAI_ROLE_ATTACK;
        if (wai->targetX >= 0) wai_attackMove(context, e, wai->targetX, wai->targetY);
        return;
    }
    if ((next + 3 >= wai->routeCount || wai->routes[next + 3] == 0xFFFF) &&
        u->type >= WAR_UNIT_CONJURER && u->type <= WAR_UNIT_MEDIVH)
    {
        info->role = 0;
        return;
    }
    info->route = (s16)next;
    wai_attackMove(context, e, wai->routes[next + 1], wai->routes[next + 2]);
}

// A route unit at rest. Only the first waypoint gathers: the units wait
// there until the group is complete, then walk the rest without stopping.
static void wai_routeStep(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info)
{
    s16 r = info->route;
    if (r < 0 || r + 2 >= wai->routeCount) { info->role = WAI_ROLE_ATTACK; return; }
    s32 wx = wai->routes[r + 1], wy = wai->routes[r + 2];
    s32 ux, uy;
    wai_tile(context, e, &ux, &uy);
    // arrived within 4 tiles (as the original), or stuck on the way
    if (wai_tileDistance(ux, uy, wx, wy) > 4 && info->still < 300)
    {
        wai_attackMove(context, e, wx, wy);
        return;
    }
    if (!info->gather)
    {
        wai_routeAdvance(context, e, u, info);
        return;
    }
    if (info->waiting) return;
    info->waiting = 1;
    wai->routes[r]++;
    if (wai->routes[r] < wai->groupSize) return;

    // the group is complete: everyone waiting here moves on
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* other = units->items[i];
        if (!wai_alive(context, other)) continue;
        WarAIUnit* oi = wai_unitInfo(context, other);
        if (!oi || oi->id != other->id || oi->role != WAI_ROLE_ROUTE || oi->route != r || !oi->waiting) continue;
        if (wai->routes[r] > 0) wai->routes[r]--;
        wai_routeAdvance(context, other, we_getUnitComponent(context, other), oi);
    }
    if (++wai->groupsLaunched >= MAX(wai->lv->groupEvery[0], 1))
    {
        wai->groupsLaunched = 0;
        if (wai->groupSize < wai->lv->groupMax[0]) wai->groupSize++;
    }
}

// The original's call for help (0x10B1C, the human's units): idle friendly
// fighters in the 3x3 around a unit under attack take on its attacker.
static void wai_alertFriends(WarContext* context, WarEntity* e, WarUnitComponent* u, WarEntity* attacker)
{
    s32 x, y;
    wai_tile(context, e, &x, &y);
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 i = 1; i < MAX_ENTITIES_COUNT; i++)
    {
        WarEntity* f = &manager->entities[i];
        if (f == e || !f->id || !wu_isUnit(f) || !wai_alive(context, f)) continue;
        WarUnitComponent* fu = we_getUnitComponent(context, f);
        if (!fu || wai_isWorkerType(fu->type) || wai_isBuildingType(fu->type) || !wai_hasStats(fu->type)) continue;
        if (wai_isEnemy(u->player, fu->player) || fu->player != u->player) continue;
        s32 fx, fy;
        wai_tile(context, f, &fx, &fy);
        if (ABS(fx - x) > 1 || ABS(fy - y) > 1) continue;
        WarAIUnit* fi = wai_unitInfo(context, f);
        if (!fi || fi->id != f->id || fi->attacker || !wst_isIdle(context, f)) continue;
        fi->attacker = attacker->id;
        wai_attack(context, f, attacker);
    }
}

// The original's answer to an attacker (0x10D34), the first thing a think
// does. true: the think is done.
static bool wai_answerAttacker(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info, bool computer)
{
    if (!info->attacker) return false;
    WarEntity* a = we_findEntity(context, info->attacker);
    if (!a || !wai_alive(context, a)) { info->attacker = 0; return false; }
    if (wai->lv->alliance[u->player] == 2) { info->attacker = 0; return false; }
    if (wai_isBuildingType(u->type)) { info->attacker = 0; return false; }
    if (u->type == WAR_UNIT_WOUNDED) return false;
    if (u->type == WAR_UNIT_GRIZELDA || u->type == WAR_UNIT_GARONA || wai_isWorkerType(u->type))
    {
        if (computer && wai_isWorkerType(u->type)) return false;   // its think runs the economy
        // (approximate: the original picks a tile away from the attacker
        // with a random offset, 0x107A0)
        s32 ux, uy, ax, ay;
        wai_tile(context, e, &ux, &uy);
        wai_tile(context, a, &ax, &ay);
        s32 fx = CLAMP(ux + 4 * SIGN(ux - ax), 0, MAP_TILES_WIDTH - 1), fy = CLAMP(uy + 4 * SIGN(uy - ay), 0, MAP_TILES_HEIGHT - 1);
        info->attacker = 0;
        if (wst_canSubmitTransition(context, e, WAR_INTERRUPT_AI_ORDER))
            wai_order(context, e, (WarStateBase*)wst_createMoveState(context, e, 2, arrayArg(vec2, wai_tileCenter(ux, uy), wai_tileCenter(fx, fy)), false));
        return true;
    }
    if (we_getAttackTarget(context, e) == a) return false;
    wai_attack(context, e, a);
    if (!computer) wai_alertFriends(context, e, u, a);
    return true;
}

// The original's idle soldier of the human (0x10908): of the hostile units
// in a square around it (twice its range + 1, wider when nothing is in
// range), the best by priority - distance squared (at least 1), out of
// range only if nearer than the best so far; the current target and the
// last attacker first, then the square in row order; a building gives way
// to the attacker.
static WarEntity* wai_acquire(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info, bool acceptOutOfRange, WarEntity* current)
{
    s32 cx, cy;
    wai_tile(context, e, &cx, &cy);
    const s32 range = wai_attackRange(u->type);
    const s32 minRange = (u->type == WAR_UNIT_CATAPULT_HUMANS || u->type == WAR_UNIT_CATAPULT_ORCS) ? 3 : 0;
    const bool human = wai->lv->alliance[u->player] == 0;
    const WarAITarget* best = NULL;
    s32 bestScore = -1, bestDistance = 0xFFFF;
    WarEntity* attacker = info->attacker ? we_findEntity(context, info->attacker) : NULL;

#define WAI_SCORE(t) do { \
        if (!wai_isEnemy(u->player, (t)->player)) break; \
        s32 d_ = wai_targetDistance((t), cx, cy); \
        if (d_ < minRange) break; \
        if (d_ > range && (!acceptOutOfRange || d_ > bestDistance)) break; \
        s32 s_ = (s32)(t)->priority - d_ * d_; \
        if (s_ < 0) s_ = 1; \
        if (s_ > bestScore) { best = (t); bestScore = s_; bestDistance = d_; } \
    } while (0)

    for (s32 i = 0; i < wai_targetCount; i++)
        if (wai_targets[i].e == current) { WAI_SCORE(&wai_targets[i]); break; }
    for (s32 i = 0; attacker && i < wai_targetCount; i++)
        if (wai_targets[i].e == attacker) { WAI_SCORE(&wai_targets[i]); break; }

    s32 half = range, size = 2 * range + 1;
    if (!best || bestDistance > range) { half += human ? 1 : 2; size += human ? 2 : 4; }
    const s32 x0 = MAX(cx - half, 0), y0 = MAX(cy - half, 0);
    const s32 x1 = MIN(cx - half + size, MAP_TILES_WIDTH), y1 = MIN(cy - half + size, MAP_TILES_HEIGHT);
    // candidates in the order the square's rows meet them (a building at
    // its first tile in the square)
    static s32 order[MAX_ENTITIES_COUNT];
    static s32 key[MAX_ENTITIES_COUNT];
    s32 n = 0;
    for (s32 i = 0; i < wai_targetCount; i++)
    {
        const WarAITarget* t = &wai_targets[i];
        s32 tx = MAX(t->x, x0), ty = MAX(t->y, y0);
        if (tx >= x1 || ty >= y1 || t->x + t->w <= x0 || t->y + t->h <= y0) continue;
        s32 k = ty * MAP_TILES_WIDTH + tx, j = n++;
        while (j > 0 && key[j - 1] > k) { key[j] = key[j - 1]; order[j] = order[j - 1]; j--; }
        key[j] = k; order[j] = i;
    }
    for (s32 j = 0; j < n; j++) WAI_SCORE(&wai_targets[order[j]]);
#undef WAI_SCORE

    WarEntity* target = best ? best->e : NULL;
    if (best && best->building && attacker) target = attacker;
    return target;
}

static bool wai_humanAcquire(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info, bool acceptOutOfRange)
{
    WarEntity* current = we_getAttackTarget(context, e);
    WarEntity* target = wai_acquire(context, e, u, info, acceptOutOfRange, current);
    if (!target || target == current) return false;
    wai_attack(context, e, target);
    return true;
}

// While attacking a unit, the original's attack order (0x1143C via 0x2EFEC)
// looks again for the best target within range, the current one scored
// first (0x10908 without out-of-range targets), every unit, both sides.
static bool wai_retarget(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info)
{
    if (!wai_hasStats(u->type) || wai_isWorkerType(u->type) || wai_isHeroType(u->type)) return false;
    WarStateBase* st = wst_getActiveState(context, e);
    if (!st || st->type != WAR_STATE_ATTACK) return false;
    WarEntity* current = we_getAttackTarget(context, e);
    if (!current || !wu_isUnit(current)) return false;
    // once a blow cycle: the attack order runs at its script's checkpoint
    if (context->gameTime < info->retargetAt) return false;
    info->retargetAt = context->gameTime + (f32)MAX(wact_getActionDuration(context, e, WAR_ACTION_TYPE_ATTACK), 1) * 0.03f;
    if (u->player == 0 && !wai_autoHuman && info->attacker == current->id)
        wai_alertFriends(context, e, u, current);   // (0x1143C)
    return wai_humanAcquire(context, e, u, info, false);
}

static void wai_soldierThink(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info)
{
    // the original's radius (0x2A1D0): catapults their range + 2, guards
    // 4, everyone else 8
    s32 radius = (u->type == WAR_UNIT_CATAPULT_HUMANS || u->type == WAR_UNIT_CATAPULT_ORCS) ? wai_attackRange(u->type) + 2
               : info->role == WAI_ROLE_GUARD ? 4 : 8;
    s32 ux, uy;
    wai_tile(context, e, &ux, &uy);
    WarEntity* target = wai_findTargetEx(context, ux, uy, radius, wai_attackRange(u->type));
    if (target)
    {
        // (the attack order's first tick, 0x2EFEC: the best within range,
        // this target scored first, before any blow)
        WarEntity* better = wai_acquire(context, e, u, info, false, target);
        if (better) target = better;
    }
    if (target && wai_attack(context, e, target))
    {
        if (info->role == WAI_ROLE_GUARD) info->role = WAI_ROLE_ATTACK;
        return;
    }

    // a damaged town hall: units near home turn on enemies close to it
    WarEntity* hall = wai_townHall(context, u->player);
    if (hall)
    {
        WarUnitComponent* hu = we_getUnitComponent(context, hall);
        // (0x2A258: the hall's tile + 1, 1; within 30 of it; 10 around it)
        s32 hx = hu ? hu->tilex + 1 : 0, hy = hu ? hu->tiley + 1 : 0;
        if (hu && hu->hp < hu->maxhp && wai_tileDistance(ux, uy, hx, hy) <= 30)
        {
            target = wai_findTargetEx(context, hx, hy, 10, 1);
            if (target && wai_attack(context, e, target)) return;
        }
    }

    switch (info->role)
    {
        case WAI_ROLE_ATTACK:
        {
            s32 tx, ty;
            if (wai_target(u->player, &tx, &ty) && wai_tileDistance(ux, uy, tx, ty) > 3)
                wai_attackMove(context, e, tx, ty);
            break;
        }
        case WAI_ROLE_ROUTE:
            wai_routeStep(context, e, u, info);
            break;
        case WAI_ROLE_GUARD:
        case WAI_ROLE_POST:
            if (wai_tileDistance(ux, uy, info->postX, info->postY) > 1)
                wai_attackMove(context, e, info->postX, info->postY);
            break;
        default:
            break;
    }
}

// --------------------------------------------------------------- casters

static bool wai_cast(WarContext* context, WarEntity* e, WarSpellType spell, WarEntity* target, vec2 position)
{
    const WarSpellStats* stats = wu_getSpellStats(spell);
    WarUnitComponent* u = we_getUnitComponent(context, e);
    if (!stats || u->mana < stats->manaCost || !wst_canSubmitTransition(context, e, WAR_INTERRUPT_AI_ORDER))
        return false;
    return wai_order(context, e, (WarStateBase*)wst_createCastState(
        context, e, spell, target ? target->id : 0, target ? wu_getUnitCenterPosition(context, target) : position));
}

// Research as the level gives it to the computer (player 1's column); the
// human side (autoplay) has what it has.
static bool wai_spellKnown(WarContext* context, s32 group)
{
    // (the level header's research for player 1: 0x50018 etc.)
    WarPlayerInfo* pi = &context->map->players[wai_self == 0 ? 0 : 1];
    return pi->upgrades[group].level > 0;
}

static WarEntity* wai_woundedFriend(WarContext* context, WarUnitComponent* caster, s32 cx, s32 cy, s32 radius)
{
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        if (!wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || u->player != caster->player || wai_isBuildingType(u->type) || u->hp >= u->maxhp) continue;
        s32 ux, uy;
        wai_tile(context, e, &ux, &uy);
        if (wai_tileDistance(cx, cy, ux, uy) <= radius) return e;
    }
    return NULL;
}

// The original's last resort (0x2A030): with the town hall gone, a unit on
// the hall's spot steps off it, so the hall can be rebuilt there.
static void wai_leaveHallSite(WarContext* context, WarEntity* e, WarUnitComponent* u)
{
    if (wai_townHall(context, u->player)) return;
    for (s32 i = 0; i < wai->planCount; i++)
    {
        const WarAIPlan* plan = &wai->plan[i];
        if (plan->player != u->player || !wai_isHallType(plan->type)) continue;
        s32 ux, uy;
        wai_tile(context, e, &ux, &uy);
        if (ux < plan->x || ux > plan->x + 2 || uy < plan->y || uy > plan->y + 2) return;
        // (approximate: the original picks a free tile next to it, 0x29C9C)
        wai_attackMove(context, e, plan->x + 1, MIN(plan->y + 4, MAP_TILES_HEIGHT - 1));
        return;
    }
}

// A caster's first candidate in the square of side `size` around (x, y), in
// row order (the original's box scans, 0x2A2E4): `pick` says yes or no.
typedef bool (*WaiCasterPick)(WarContext* context, WarEntity* caster, WarEntity* e, WarUnitComponent* u, s32* score);
static WarEntity* wai_scanBox(WarContext* context, WarEntity* caster, s32 x, s32 y, s32 size, WaiCasterPick pick, bool best)
{
    s32 x0 = MAX(x - size / 2, 0), y0 = MAX(y - size / 2, 0);
    s32 x1 = MIN(x - size / 2 + size, MAP_TILES_WIDTH), y1 = MIN(y - size / 2 + size, MAP_TILES_HEIGHT);
    WarEntity* chosen = NULL;
    s32 chosenKey = INT32_MAX, chosenScore = -1;
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 i = 1; i < MAX_ENTITIES_COUNT; i++)
    {
        WarEntity* e = &manager->entities[i];
        if (!e->id || !wu_isUnit(e) || !wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u) continue;
        s32 ex, ey;
        wai_tile(context, e, &ex, &ey);
        if (ex < x0 || ex >= x1 || ey < y0 || ey >= y1) continue;
        s32 score = 0;
        if (!pick(context, caster, e, u, &score)) continue;
        s32 key = ey * MAP_TILES_WIDTH + ex;
        if (best ? (score > chosenScore || (score == chosenScore && key < chosenKey)) : key < chosenKey)
        {
            chosen = e;
            chosenKey = key;
            chosenScore = score;
        }
    }
    return chosen;
}

static bool wai_pickWounded(WarContext* context, WarEntity* caster, WarEntity* e, WarUnitComponent* u, s32* score)
{
    WarUnitComponent* cu = we_getUnitComponent(context, caster);
    if (wai_isEnemy(cu->player, u->player) || u->player != cu->player) return false;
    if (wai_isBuildingType(u->type) || u->hp >= u->maxhp) return false;
    *score = u->maxhp - u->hp;   // the largest loss
    return true;
}

static bool wai_pickWalkingFriend(WarContext* context, WarEntity* caster, WarEntity* e, WarUnitComponent* u, s32* score)
{
    WarUnitComponent* cu = we_getUnitComponent(context, caster);
    NOT_USED(score);
    // (0x2A57C / 0x2A714: a friendly unit on the move, its health above
    // half; the exact health test is approximate)
    return u->player == cu->player && !wai_isBuildingType(u->type) && wst_isMoving(context, e) && u->hp * 2 > u->maxhp;
}

static bool wai_pickCorpse(WarContext* context, WarEntity* caster, WarEntity* e, WarUnitComponent* u, s32* score)
{
    NOT_USED(context); NOT_USED(caster); NOT_USED(e); NOT_USED(score);
    return u->type == WAR_UNIT_HUMAN_CORPSE || u->type == WAR_UNIT_ORC_CORPSE;
}

static bool wai_pickHostile(WarContext* context, WarEntity* caster, WarEntity* e, WarUnitComponent* u, s32* score)
{
    WarUnitComponent* cu = we_getUnitComponent(context, caster);
    NOT_USED(context); NOT_USED(e); NOT_USED(score);
    return wai_isEnemy(cu->player, u->player) && u->type < WAR_UNIT_HUMAN_CORPSE;
}

// The original's summoning (0x27AB0): up to four times while the mana
// lasts, a unit next to the caster (war1's cast state does not summon).
static bool wai_summon(WarContext* context, WarEntity* e, WarUnitComponent* u, WarSpellType spell)
{
    const WarSpellStats* stats = wu_getSpellStats(spell);
    const WarSpellMapping* mapping = wu_getSpellMapping(spell);
    if (!stats || !mapping || u->mana < stats->manaCost) return false;
    WarMap* map = context->map;
    bool any = false;
    for (s32 i = 0; i < 4 && u->mana >= stats->manaCost; i++)
    {
        vec2 tile = wu_getUnitCenterTile(context, e);
        vec2 spawn = wpath_findEmptyTile(&map->finder, (s32)tile.x, (s32)tile.y);
        WarEntity* s2 = we_createUnit(context, CREATE_UNIT_ARGS_INIT(
            .type = mapping->mappedType, .x = (s32)spawn.x, .y = (s32)spawn.y, .player = u->player,
            .resourceKind = WAR_RESOURCE_NONE, .amount = 0, .addToMap = true));
        if (!s2) break;
        we_setInitialIdleState(context, s2);
        vec2 size = wu_getUnitSize(context, s2);
        wpath_setStaticEntity(&map->finder, (s32)spawn.x, (s32)spawn.y, (s32)size.x, (s32)size.y, s2->id);
        WarEntity* anim = we_createEntity(context, WAR_ENTITY_TYPE_ANIMATION, true);
        we_addAnimationsComponent(context, anim);
        wanim_createSpellAnimation(context, anim, wmap_tileToMapCoordinatesV(spawn, true));
        u->mana -= stats->manaCost;
        any = true;
    }
    if (any)
    {
        u->invisible = false;
        u->invisibilityTime = 0;
    }
    return any;
}

// The original's area spells (0x2A864): within 25 tiles of the re-aim
// target, at the first enemy in the 11-tile square around that target, 2
// tiles off at random either way.
static bool wai_castArea(WarContext* context, WarEntity* e, WarUnitComponent* u, WarSpellType spell)
{
    s32 tx, ty, ux, uy;
    if (!wai_target(u->player, &tx, &ty)) return false;
    wai_tile(context, e, &ux, &uy);
    if (wai_tileDistance(ux, uy, tx, ty) > 25) return false;
    WarEntity* t = wai_scanBox(context, e, tx, ty, 11, wai_pickHostile, false);
    if (!t) return false;
    s32 x, y;
    wai_tile(context, t, &x, &y);
    x = CLAMP(x + wai_rand(5) - 2, 0, MAP_TILES_WIDTH - 1);
    y = CLAMP(y + wai_rand(5) - 2, 0, MAP_TILES_HEIGHT - 1);
    return wai_cast(context, e, spell, NULL, wai_tileCenter(x, y));
}

// The original's caster think (0x2A9E4): its spells by mana (a computer
// knows what the level gives player 1), then the soldier's home defence,
// then back to its post unless it attacks or walks a route.
static void wai_casterThink(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info)
{
    s32 mana = u->mana;
    vec2 here = wu_getUnitCenterPosition(context, e);
    s32 ux, uy;
    wai_tile(context, e, &ux, &uy);
    switch (u->type)
    {
        case WAR_UNIT_CLERIC:
        {
            if (wai_spellKnown(context, 8) && mana >= 125)
            {
                WarEntity* f = wai_scanBox(context, e, ux, uy, 21, wai_pickWalkingFriend, false);
                if (f && wai_cast(context, e, WAR_SPELL_INVISIBILITY, f, here)) return;
            }
            if (wai_spellKnown(context, 6) && mana >= 3)
            {
                WarEntity* w = wai_scanBox(context, e, ux, uy, 21, wai_pickWounded, true);
                if (w && wai_cast(context, e, WAR_SPELL_HEALING, w, here)) return;
            }
            break;
        }
        case WAR_UNIT_CONJURER:
            if (mana >= 255)
            {
                if (wai_spellKnown(context, 5)) { wai_summon(context, e, u, WAR_SUMMON_WATER_ELEMENTAL); return; }
                if (wai_spellKnown(context, 3)) { wai_summon(context, e, u, WAR_SUMMON_SCORPION); return; }
            }
            if (wai_spellKnown(context, 4) && mana >= 150 && wai_castArea(context, e, u, WAR_SPELL_RAIN_OF_FIRE)) return;
            break;
        case WAR_UNIT_NECROLYTE:
            if (wai_spellKnown(context, 8) && mana >= 125)
            {
                WarEntity* f = wai_scanBox(context, e, ux, uy, 21, wai_pickWalkingFriend, false);
                if (f && wai_cast(context, e, WAR_SPELL_UNHOLY_ARMOR, f, here)) return;
            }
            if (wai_spellKnown(context, 6) && mana >= 70)
            {
                WarEntity* c = wai_scanBox(context, e, ux, uy, 21, wai_pickCorpse, false);
                if (c && wai_cast(context, e, WAR_SPELL_RAISE_DEAD, NULL, wu_getUnitCenterPosition(context, c))) return;
            }
            break;
        case WAR_UNIT_WARLOCK:
            if (mana >= 255)
            {
                if (wai_spellKnown(context, 5)) { wai_summon(context, e, u, WAR_SUMMON_DAEMON); return; }
                if (wai_spellKnown(context, 3)) { wai_summon(context, e, u, WAR_SUMMON_SPIDER); return; }
            }
            if (wai_spellKnown(context, 4) && mana >= 50 && wai_castArea(context, e, u, WAR_SPELL_POISON_CLOUD)) return;
            break;
        default:   // Medivh
            if (mana >= 255) { wai_summon(context, e, u, WAR_SUMMON_DAEMON); return; }
            if (mana >= 60) { wai_summon(context, e, u, WAR_SUMMON_SPIDER); return; }
            wai_soldierThink(context, e, u, info);
            return;
    }

    // home defence, as a soldier's
    WarEntity* hall = wai_townHall(context, u->player);
    if (hall)
    {
        WarUnitComponent* hu = we_getUnitComponent(context, hall);
        s32 hx = hu ? hu->tilex + 1 : 0, hy = hu ? hu->tiley + 1 : 0;
        if (hu && hu->hp < hu->maxhp && wai_tileDistance(ux, uy, hx, hy) <= 30)
        {
            WarEntity* target = wai_findTargetEx(context, hx, hy, 10, 1);
            if (target && wai_attack(context, e, target)) return;
        }
    }
    if (info->role == WAI_ROLE_ATTACK || info->role == WAI_ROLE_ROUTE)
    {
        wai_leaveHallSite(context, e, u);
        return;
    }
    if (ux != info->postX || uy != info->postY)
        wai_attackMove(context, e, info->postX, info->postY);
}

// ----------------------------------------------------------- unit roles

static bool wai_takeOverPost(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info)
{
    if (wai_rand(4) == 0) return false;   // 3 times in 4
    for (s32 i = 0; i < wai->postCount; i++)
    {
        WarAIPost* post = &wai->posts[i];
        if (post->player != u->player || post->type != u->type) continue;
        WarEntity* holder = post->id ? we_findEntity(context, post->id) : NULL;
        if (holder && wai_alive(context, holder)) continue;
        post->id = e->id;
        info->role = WAI_ROLE_POST;
        info->postX = post->x;
        info->postY = post->y;
        return true;
    }
    return false;
}

static void wai_pickRoute(WarContext* context, WarAIUnit* info)
{
    NOT_USED(context);
    info->role = WAI_ROLE_ATTACK;
    if (wai->routeCount < 2 || wai->routes[0] == 0xFFFF) return;
    s32 r = wai_rand(100);
    for (s32 k = 0; k + 1 < wai->routeCount && wai->routes[k] != 0xFFFF; k += 2)
    {
        s32 weight = wai->routes[k];
        if (r < weight || wai->routes[k + 2] == 0xFFFF)
        {
            s32 idx = wai->routes[k + 1];
            if (idx + 2 < wai->routeCount && wai->routes[idx] != 0xFFFF)
            {
                info->role = WAI_ROLE_ROUTE;
                info->route = (s16)idx;
                info->gather = 1;
                info->waiting = 0;
            }
            return;
        }
        r -= weight;
    }
}

// The original runs an idle unit's think once every 9 game ticks (its idle
// script's cadence), and on the first tick it is idle again; production
// countdowns and all of a unit's decisions go by thinks.
#define WAI_THINK_TICKS 9
static bool wai_thinkDue(WarContext* context, WarEntity* e, WarAIUnit* info)
{
    if (!wst_isIdle(context, e))
    {
        info->thinkWait = 0;
        return false;
    }
    if (info->thinkWait > 0)
    {
        info->thinkWait--;
        return false;
    }
    info->thinkWait = WAI_THINK_TICKS / WAI_STEP - 1;
    return true;
}

static void wai_assignRole(WarContext* context, WarEntity* e, WarUnitComponent* u, WarAIUnit* info, bool levelStart)
{
    memset(info, 0, sizeof(*info));
    info->id = e->id;
    info->route = -1;
    s32 ux, uy;
    wai_tile(context, e, &ux, &uy);
    info->postX = (s16)ux;
    info->postY = (s16)uy;

    if (wai_isBuildingType(u->type))
    {
        info->timer = 0;   // the first think loads the interval
        if (wai_isHallType(u->type))
        {
            wai->homeX[u->player] = u->tilex + u->sizex / 2;
            wai->homeY[u->player] = u->tiley + u->sizey / 2;
        }
        return;
    }
    if (wai_isWorkerType(u->type) || wai_isHeroType(u->type)) return;

    if (wai_isMonsterType(u->type))
    {
        info->role = levelStart ? WAI_ROLE_GUARD : WAI_ROLE_ATTACK;
        return;
    }
    if (wai->lv->flags & 0x01)
    {
        info->role = WAI_ROLE_GUARD;
        return;
    }
    if (levelStart)
    {
        // the level's own soldiers hold their posts
        info->role = WAI_ROLE_POST;
        if (wai->postCount < WAI_POST_MAX)
            wai->posts[wai->postCount++] = (WarAIPost){ u->type, u->player, (s16)ux, (s16)uy, e->id };
        return;
    }
    if (wai_takeOverPost(context, e, u, info)) return;
    if (u->player == 0) return;   // autoplay: kept home until its wave
    wai_pickRoute(context, info);
}

// ------------------------------------------------------------------ the AI

void wai_initAIPlayers(WarContext* context)
{
    WarMap* map = context->map;
    WarResource* levelInfo = wres_getOrCreateResource(context, map->levelInfoIndex);

    if (!wai)
        wai = (WarAIState*)wm_alloc(sizeof(WarAIState));
    if (!wai) return;
    memset(wai, 0, sizeof(*wai));
    wai->lv = levelInfo->levelInfo.ai;
    if (!wai->lv || map->customGame.enabled)
    {
        wai->lv = NULL; // custom games: war1's own behaviour
        return;
    }

    wai->rng = 0x9E3779B97F4A7C15ull ^ (u64)map->levelInfoIndex;
#ifdef WAR_MEMDEBUG
    printf("ai: level start %d,%d\n", levelInfo->levelInfo.startX, levelInfo->levelInfo.startY);
#endif
    wai->nextStep = context->gameTime;
    wai->reaim = 1;
    wai->wave = 6000;
    wai->targetX = wai->targetY = -1;
    wai->targetX0 = wai->targetY0 = -1;
    wai->wave0 = 1500;
    wai->groupSize = MAX(wai->lv->groupSize[0], 1);
#ifdef WAR_MEMDEBUG
    printf("ai: group %d/%d/%d (p1 %d/%d/%d)\n", wai->lv->groupSize[0], wai->lv->groupEvery[0], wai->lv->groupMax[0], wai->lv->groupSize[1], wai->lv->groupEvery[1], wai->lv->groupMax[1]);
#endif
    wai->routeCount = wai->lv->routeWordsCount;
    memcpy(wai->routes, wai->lv->routeWords, (size_t)wai->routeCount * sizeof(u16));
    for (s32 p = 0; p < MAX_PLAYERS_COUNT; p++)
    {
        wai->homeX[p] = levelInfo->levelInfo.startX;
        wai->homeY[p] = levelInfo->levelInfo.startY;
        // the computer's research: what the level gives it, up to level 2
        if (p > 0 && wai_isComputer(context, p))
        {
            WarPlayerInfo* pi = &map->players[p];
            for (s32 g = 0; g < MAX_UPGRADES_COUNT; g++)
            {
                pi->upgrades[g].level = pi->upgrades[g].allowed;
                pi->upgrades[g].allowed = 2;
            }
        }
    }
}

static void wai_countPlayers(WarContext* context)
{
    for (s32 p = 0; p < MAX_PLAYERS_COUNT; p++)
        wai->casters[p] = wai->workers[p] = wai->farms[p] = wai->builders[p] = wai->goldWorkers[p] = 0;
    wai->hasMill = wai->hasSmith = wai->mill0 = wai->smith0 = false;

    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        if (!wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || !wai_isComputer(context, u->player)) continue;
        s32 p = u->player;
        if (wai_isCasterType(u->type)) wai->casters[p]++;
        else if (wai_isWorkerType(u->type))
        {
            wai->workers[p]++;
            if (wst_isRepairing(context, e) || wst_isRepairing2(context, e)) wai->builders[p]++;
            else if (wst_isGatheringGold(context, e) || wst_isMining(context, e)) wai->goldWorkers[p]++;
        }
        else if (!u->building)
        {
            if (u->type == WAR_UNIT_FARM_HUMANS || u->type == WAR_UNIT_FARM_ORCS) wai->farms[p]++;
            bool* mill = p == 0 ? &wai->mill0 : &wai->hasMill;
            bool* smith = p == 0 ? &wai->smith0 : &wai->hasSmith;
            if (u->type == WAR_UNIT_LUMBERMILL_HUMANS || u->type == WAR_UNIT_LUMBERMILL_ORCS) *mill = true;
            if (u->type == WAR_UNIT_BLACKSMITH_HUMANS || u->type == WAR_UNIT_BLACKSMITH_ORCS) *smith = true;
        }
    }
}

// The human reaching a neutral (player 4) unit frees it: it joins, and the
// computer goes all-out.
static void wai_rescue(WarContext* context)
{
    if (!wai->lv || wai->lv->alliance[4] != 2) return;
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* n = units->items[i];
        if (!wai_alive(context, n)) continue;
        WarUnitComponent* nu = we_getUnitComponent(context, n);
        if (!nu || nu->player != 4 || wai_isBuildingType(nu->type) || !wai_hasStats(nu->type)) continue;
        for (s32 j = 0; j < units->count; j++)
        {
            WarEntity* h = units->items[j];
            if (!wai_alive(context, h)) continue;
            WarUnitComponent* hu = we_getUnitComponent(context, h);
            if (!hu || hu->player != 0 || wai_isBuildingType(hu->type)) continue;
            s32 nx, ny, hx, hy;
            wai_tile(context, n, &nx, &ny);
            wai_tile(context, h, &hx, &hy);
            if (wai_tileDistance(nx, ny, hx, hy) > 1) continue;
#ifdef WAR_MEMDEBUG
            printf("ai: rescued unit type %d\n", nu->type);
#endif
            // a freed worker passes unnoticed; anyone else sends every
            // computer player all-out
            if (!wai_isWorkerType(nu->type))
            {
                for (s32 p = 1; p < MAX_PLAYERS_COUNT; p++)
                    if (wai_isComputer(context, p)) wai_allOut(context, p);
            }
            if (nu->type == WAR_UNIT_WOUNDED)
            {
                // a freed wounded soldier joins as an archer
                WarEntity* archer = we_createUnit(context, CREATE_UNIT_ARGS_INIT(
                    .type = WAR_UNIT_ARCHER, .x = nx, .y = ny, .player = 0, .addToMap = true));
                if (archer)
                {
                    we_setInitialIdleState(context, archer);
                    we_removeEntityById(context, n->id);
                    return;   // the unit list changed: the rest next tick
                }
            }
            nu->player = 0;
            break;
        }
    }
}

// Autoplay: the human side gathers at home and attacks with everything
// once there are enough (8, then 2 more each wave, up to 16).
static void wai_humanWave(WarContext* context)
{
    wai->wave0 -= WAI_STEP;
    if (wai->wave0 > 0) return;
    wai->wave0 = 1500;
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    s32 ready = 0;
    for (s32 pass = 0; pass < 2; pass++)
    {
        for (s32 i = 0; i < units->count; i++)
        {
            WarEntity* e = units->items[i];
            if (!wai_alive(context, e)) continue;
            WarUnitComponent* u = we_getUnitComponent(context, e);
            if (!u || u->player != 0 || wai_isBuildingType(u->type) || wai_isWorkerType(u->type) ||
                wai_isHeroType(u->type) || !wai_hasStats(u->type)) continue;
            WarAIUnit* info = wai_unitInfo(context, e);
            if (!info || info->id != e->id || info->role == WAI_ROLE_ATTACK) continue;
            if (pass == 0) ready++;
            else info->role = WAI_ROLE_ATTACK;
        }
        // without a barracks nothing more is coming: go with what there is
        bool more = wai_townHall(context, 0) &&
                    wu_getNumberOfBuildingsOfType(context, 0, wu_getUnitTypeForRace(WAR_UNIT_BARRACKS_HUMANS, context->map->players[0].race), true) > 0;
        if (pass == 0 && (ready == 0 || (more && ready < MIN(8 + 2 * wai->waves0, 16)))) return;
    }
    wai->waves0++;
}

static void wai_step(WarContext* context, bool levelStart)
{
    WarMap* map = context->map;

    // the clock
    wai->reaim -= WAI_STEP;
    if (wai->reaim <= 0)
    {
        wai->reaim += 800;
        wai_reaim(context);
    }
    if (wai->wave > 0)
    {
        wai->wave -= WAI_STEP;
        if (wai->wave <= 0)
        {
            u16 f = wai->lv->flags;
            wai->wave = (f & 0x84) ? 350 : (f & 0x40) ? 6000 : -1;
            wai_launchWave(context);
        }
    }

    // buildings lost send their player all-out (a keep: everyone)
    for (s32 i = 0; i < wai->planCount; i++)
    {
        WarAIPlan* plan = &wai->plan[i];
        if (!plan->id) continue;
        WarEntity* e = we_findEntity(context, plan->id);
        if (e && wai_alive(context, e)) continue;
        plan->id = 0;
        if (plan->type == WAR_UNIT_STORMWIND || plan->type == WAR_UNIT_BLACKROCK)
        {
            for (s32 p = 1; p < 4; p++) wai_allOut(context, p);
        }
        else if (plan->player != 0)   // autoplay: the human side stays to defend
        {
            wai_allOut(context, plan->player);
        }
    }

    wai_countPlayers(context);
    wai_rescue(context);
    wai_gatherTargets(context);
    if (wai_autoHuman) wai_humanWave(context);

    // the units think in slot order, as in the original's unit table
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 i = 1; i < MAX_ENTITIES_COUNT; i++)
    {
        WarEntity* e = &manager->entities[i];
        if (!e->id || !wu_isUnit(e) || !wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || u->player >= MAX_PLAYERS_COUNT) continue;
        if (u->player == 0 && !wai_autoHuman)
        {
            // the human's own units: the original's think for a human unit
            WarAIUnit* hinfo = wai_unitInfo(context, e);
            if (!hinfo) continue;
            if (hinfo->id != e->id) { memset(hinfo, 0, sizeof(*hinfo)); hinfo->id = e->id; hinfo->route = -1; }
            wai_self = 0;
            if (wai_retarget(context, e, u, hinfo)) continue;
            if (wai_isBuildingType(u->type) || !wai_hasStats(u->type) || !wai_thinkDue(context, e, hinfo)) continue;
            wai_self = 0;
            if (wai_answerAttacker(context, e, u, hinfo, false)) continue;
            if (wai_isWorkerType(u->type) || u->type == WAR_UNIT_WOUNDED || u->type == WAR_UNIT_GRIZELDA || u->type == WAR_UNIT_GARONA) continue;
            wai_humanAcquire(context, e, u, hinfo, true);
            continue;
        }
        if (!wai_isComputer(context, u->player)) continue;
        wai_self = u->player;
        WarAIUnit* info = wai_unitInfo(context, e);
        if (!info) continue;
        if (info->id != e->id)
        {
            wai_assignRole(context, e, u, info, levelStart);
            if (levelStart && wai_isBuildingType(u->type) && wai->planCount < WAI_PLAN_MAX)
                wai->plan[wai->planCount++] = (WarAIPlan){ u->type, u->player, (s16)u->tilex, (s16)u->tiley, e->id };
        }

        if (!wai_isBuildingType(u->type) && wai_retarget(context, e, u, info)) continue;
        if (wai_isBuildingType(u->type))
        {
            if (u->building || !wai_thinkDue(context, e, info)) continue;
            // the original's countdown (think 0x10EE8): 1 produces, 0 loads
            // the level's interval, anything else counts down
            if (info->timer == 1)
            {
                info->timer = 0;
                wai_produce(context, e, u);
            }
            else if (info->timer == 0)
                info->timer = wai->lv->interval[u->type - WAR_UNIT_FARM_HUMANS];
            else
                info->timer--;
            continue;
        }
        if (!wai_hasStats(u->type)) continue;
        // war1's units do not push past each other: notice the jammed ones
        // (about 9 s without moving while trying to)
        bool moving = wst_isMoving(context, e);
        if (moving)
        {
            s32 ux, uy;
            wai_tile(context, e, &ux, &uy);
            if (ux == info->lastX && uy == info->lastY) info->still += WAI_STEP;
            else { info->still = 0; info->lastX = (s16)ux; info->lastY = (s16)uy; }
        }
        else if (info->role != WAI_ROLE_ROUTE || !info->waiting)
        {
            info->still = 0;
        }
        if (moving && info->still >= 300)
        {
            info->still = 0;
            if (info->role == WAI_ROLE_ROUTE && !info->waiting)
            {
                info->still = 300;  // counts as arrived at its waypoint
                wai_routeStep(context, e, u, info);
                info->still = 0;
                continue;
            }
            if (wai_isWorkerType(u->type))
            {
                // the other job
                bool wasGold = wst_isGatheringGold(context, e);
                if (wasGold ? wai_chopWood(context, e, u) : wai_mineGold(context, e, u)) continue;
            }
            if (wst_canSubmitTransition(context, e, WAR_INTERRUPT_AI_ORDER))
                wai_order(context, e, (WarStateBase*)wst_createIdleState(context, e, false));
            continue;
        }
        if (!wai_thinkDue(context, e, info) || !wai_hasStats(u->type)) continue;
        if (wai_answerAttacker(context, e, u, info, true)) continue;
        if (wai_isHeroType(u->type)) continue;
        if (wai_isWorkerType(u->type)) wai_workerThink(context, e, u, info);
        else if (wai_isCasterType(u->type)) wai_casterThink(context, e, u, info);
        else wai_soldierThink(context, e, u, info);
    }
    NOT_USED(map);
}

#ifndef FRANK_BOARD
// Research (host): the AI's state in the reference build's format
// (war1-reference runtime, WAR_AILOG), every 33 ticks, for comparing.
static void wai_traceLog(WarContext* context, u32 ticks)
{
    static FILE* f;
    static bool opened;
    if (!opened) { opened = true; const char* p = getenv("WAR_AILOG"); if (p) f = fopen(p, "w"); }
    if (!f) return;
    fprintf(f, "ms %u t %u wave %d reaim %d tgt %d,%d grp %d/%d q %d %d %d %d %d flags %04x\n",
            (u32)(context->gameTime * 1000.0f), ticks, wai->wave, wai->reaim, wai->targetX, wai->targetY,
            wai->groupSize, wai->groupsLaunched, wai->quota[0], wai->quota[1], wai->quota[2], wai->quota[3],
            wai->quota[4], wai->lv->flags);
    u32 cnt[MAX_PLAYERS_COUNT][64] = {{0}}, role[MAX_PLAYERS_COUNT][64] = {{0}};
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        if (!wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || u->player >= MAX_PLAYERS_COUNT || u->type >= 64) continue;
        cnt[u->player][wu_toOriginalType(u->type)]++;   // (the reference's numbers)
        WarAIUnit* info = wai_unitInfo(context, e);
        if (u->type <= WAR_UNIT_30) role[u->player][info && info->id == e->id ? info->role & 63 : 63]++;
        if (ticks % 330 == 0)
        {
            s32 x, y;
            wai_tile(context, e, &x, &y);
            WarStateBase* st = wst_getActiveState(context, e);
            WarEntity* tgt = we_getAttackTarget(context, e);
            WarUnitComponent* tu = tgt ? we_getUnitComponent(context, tgt) : NULL;
            fprintf(f, "  u %d type %d owner %d tile %d,%d role %d route %d state %d target %d/%d\n", e->id, u->type, u->player, x, y,
                    info && info->id == e->id ? info->role : -1, info && info->id == e->id ? info->route : -1,
                    st ? (s32)st->type : -1, tu ? tu->type : -1, tu ? tu->player : -1);
        }
    }
    for (s32 p = 0; p < 5; p++)
    {
        u32 tot = 0;
        for (s32 t = 0; t < 64; t++) tot += cnt[p][t];
        if (!tot) continue;
        fprintf(f, "  p%d", p);
        for (s32 t = 0; t < 64; t++) if (cnt[p][t]) fprintf(f, " %d:%u", t, cnt[p][t]);
        fprintf(f, " | roles");
        for (s32 r = 0; r < 64; r++) if (role[p][r]) fprintf(f, " %d:%u", r, role[p][r]);
        fprintf(f, "\n");
    }
    fflush(f);
}
#endif

void wai_updateAIPlayers(WarContext* context)
{
    if (!wai || !wai->lv) return;
    // the original's AI tick, WAI_STEP at a time, on game time
    s32 guard = 0;
    while (context->gameTime >= wai->nextStep && guard++ < 8)
    {
        wai_step(context, !wai->started);
        wai->started = true;
#ifndef FRANK_BOARD
        {
            static u32 ticks;
            static WarAIState* run;
            if (run != wai || !wai->ticksSeen) { ticks = 0; run = wai; wai->ticksSeen = true; }
            const u32 before = ticks;
            ticks += WAI_STEP;
            if (before / 33 != ticks / 33) wai_traceLog(context, ticks / 33 * 33);
            static s32 watch = -2;
            if (watch == -2) watch = getenv("WAR_AIWATCH") ? atoi(getenv("WAR_AIWATCH")) : -1;
            if (watch > 0)
            {
                static FILE* wf;
                if (!wf) wf = fopen("aiwatch.txt", "w");
                WarEntity* we = we_findEntity(context, (WarEntityId)watch);
                WarEntity* wt = we ? we_getAttackTarget(context, we) : NULL;
                WarUnitComponent* wtu = wt ? we_getUnitComponent(context, wt) : NULL;
                if (wf && we) fprintf(wf, "w %u t %.3f thp %d\n", ticks, context->gameTime, wtu ? wtu->hp : -1);
            }
        }
#endif
        wai->nextStep += WAI_STEP * WAI_TICK_SECONDS;
    }
}

// ------------------------------------------------------------- objectives

// The original's win and lose checks, chosen by the level's AI flags
// (docs/ai.md "Mission objectives"). NONE when the level has no AI data.
bool wai_checkObjectives(WarContext* context, WarLevelResult* result)
{
    if (!wai || !wai->lv) return false;
    WarMap* map = context->map;
    u16 f = wai->lv->flags;
    *result = WAR_LEVEL_RESULT_NONE;

    s32 units[MAX_PLAYERS_COUNT] = {0}, buildings[MAX_PLAYERS_COUNT] = {0};
    s32 workers0 = 0, farms0 = 0, producers0 = 0, inProgress0 = 0, heroes0 = 0;
    s32 heroesAtExit = 0, unitsAtExit = 0;
    bool hall0 = false;

    // the exit zone: tiles flagged 0x20 in the level's passable data
    WarResource* levelInfo = wres_getOrCreateResource(context, map->levelInfoIndex);
    WarResource* passable = wres_getOrCreateResource(context, levelInfo->levelInfo.passableIndex);
    const u16* tileFlags = passable && passable->type == WAR_RESOURCE_TYPE_LEVEL_PASSABLE ? passable->levelPassable.data : NULL;

    WarEntityList* list = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < list->count; i++)
    {
        WarEntity* e = list->items[i];
        if (!wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || u->player >= MAX_PLAYERS_COUNT || u->type == WAR_UNIT_GOLDMINE) continue;
        if (wai_isBuildingType(u->type))
        {
            buildings[u->player]++;
            if (u->player == 0)
            {
                if (u->building || wst_isTraining(context, e)) inProgress0++;
                if (!u->building && (u->type == WAR_UNIT_FARM_HUMANS || u->type == WAR_UNIT_FARM_ORCS)) farms0++;
                if (!u->building && u->type >= WAR_UNIT_BARRACKS_HUMANS && u->type <= WAR_UNIT_TOWNHALL_ORCS) producers0++;
                if (!u->building && wai_isHallType(u->type)) hall0 = true;
            }
            continue;
        }
        if (u->type >= WAR_UNIT_HUMAN_CORPSE) continue;
        units[u->player]++;
        if (u->player != 0) continue;
        if (wai_isWorkerType(u->type)) workers0++;
        bool hero = u->type == WAR_UNIT_LOTHAR || u->type == WAR_UNIT_GARONA;
        if (hero) heroes0++;
        s32 ux, uy;
        wai_tile(context, e, &ux, &uy);
        bool atExit = tileFlags && ux >= 0 && ux < MAP_TILES_WIDTH && uy >= 0 &&
                      uy < MAP_TILES_HEIGHT && (tileFlags[uy * MAP_TILES_WIDTH + ux] & 0x20);
        if (atExit) { unitsAtExit++; if (hero) heroesAtExit++; }
    }

    // defeat: a hero that had joined is gone
    if (heroes0 > 0) wai->heroJoined = true;
    if (wai->heroJoined && heroes0 == 0)
    {
#ifdef WAR_MEMDEBUG
        printf("ai: result lose (hero) at %.0f s\n", context->gameTime);
#endif
        *result = WAR_LEVEL_RESULT_LOSE;
        return true;
    }
    WarPlayerInfo* human = &map->players[0];
    if (inProgress0 == 0)
    {
        if (units[0] == 0 && !(producers0 > 0 && human->gold >= 400))
        {
#ifdef WAR_MEMDEBUG
            printf("ai: result lose (no units) at %.0f s\n", context->gameTime);
#endif
            *result = WAR_LEVEL_RESULT_LOSE;
            return true;
        }
        if (units[0] > 0 && units[0] == workers0 && !hall0 && (human->gold < 400 || human->wood < 400))
        {
#ifdef WAR_MEMDEBUG
            printf("ai: result lose (workers only) at %.0f s\n", context->gameTime);
#endif
            *result = WAR_LEVEL_RESULT_LOSE;
            return true;
        }
    }

    // victory
    bool win = false;
    if (f & 0x01)
    {
        win = farms0 >= 6 && producers0 >= 2 && hall0;
    }
    else
    {
        bool enemiesGone = true;
        for (s32 p = 1; p <= 3; p++)
            if (units[p] > 0) enemiesGone = false;
        if (f & 0x10)
        {
            win = enemiesGone && buildings[1] == 1;
        }
        else if (f & 0x02)
        {
            win = enemiesGone;
            for (s32 p = 1; p <= 3; p++)
                if (buildings[p] > 0) win = false;
        }
        if (f & 0x84)
        {
            bool atExit = (f & 0x04) ? heroesAtExit >= MAX(wai->lv->exitHeroes, 1) : unitsAtExit > 0;
            win = ((f & 0x12) ? win : true) && atExit;
        }
    }
#ifdef WAR_MEMDEBUG
    if (win) printf("ai: result win at %.0f s\n", context->gameTime);
#endif
    if (win) *result = WAR_LEVEL_RESULT_WIN;
    return true;
}

// Host test hooks for the objective checks (host script `ai <cmd>`):
//   kill N  destroys every unit and building of players 1-3 but N buildings
//   reveal  the fog-of-war cheat (screenshots)
//   exit    hands Lothar/Garona to the human and puts them on the exit
void wai_debug(void* ctx, const char* cmd)
{
    WarContext* context = (WarContext*)ctx;
    if (!wai || !wai->lv || !context->map) return;
    WarEntityList* list = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    if (!strncmp(cmd, "kill", 4))
    {
        s32 keep = atoi(cmd + 4);
        for (s32 i = 0; i < list->count; i++)
        {
            WarEntity* e = list->items[i];
            if (!wai_alive(context, e)) continue;
            WarUnitComponent* u = we_getUnitComponent(context, e);
            if (!u || u->player < 1 || u->player > 3 || u->type >= WAR_UNIT_HUMAN_CORPSE) continue;
            if (wai_isBuildingType(u->type) && keep > 0) { keep--; continue; }
            we_takeDamage(context, e, u->hp + u->armor + 1000, 0);
        }
    }
    else if (!strncmp(cmd, "dumpres", 7))
    {
        // dumpres N: raw resource N to resN.bin (research)
        s32 idx = atoi(cmd + 7);
        WarRawResource* r = &context->warFile->resources[idx];
        printf("dumpres %d: placeholder %d compressed %d length %u data %p\n", idx, r->placeholder, r->compressed, r->length, (void*)r->data);
        if (r->data || wfile_readResource(context->warFile, idx))
        {
            char path[32];
            snprintf(path, sizeof(path), "res%d.bin", idx);
            FILE* f = fopen(path, "wb");
            if (f) { fwrite(r->data, 1, r->length, f); fclose(f); }
        }
    }
    else if (!strcmp(cmd, "reveal"))
    {
        wcheat_applyFogOfWarCheat(context, WSV_LITERAL(""));
    }
    else if (!strcmp(cmd, "exit"))
    {
        WarResource* levelInfo = wres_getOrCreateResource(context, context->map->levelInfoIndex);
        WarResource* passable = wres_getOrCreateResource(context, levelInfo->levelInfo.passableIndex);
        if (!passable || passable->type != WAR_RESOURCE_TYPE_LEVEL_PASSABLE) return;
        s32 ex = -1, ey = -1;
        for (s32 k = 0; k < MAP_TILES_WIDTH * MAP_TILES_HEIGHT && ex < 0; k++)
            if (passable->levelPassable.data[k] & 0x20) { ex = k % MAP_TILES_WIDTH; ey = k / MAP_TILES_WIDTH; }
        for (s32 i = 0; i < list->count && ex >= 0; i++)
        {
            WarEntity* e = list->items[i];
            if (!wai_alive(context, e)) continue;
            WarUnitComponent* u = we_getUnitComponent(context, e);
            if (!u || (u->type != WAR_UNIT_LOTHAR && u->type != WAR_UNIT_GARONA)) continue;
            u->player = 0;
            wu_setUnitTile(context, e, vec2i(ex, ey));
        }
    }
}

// The camera and the mouse as the game sees them (board console `cam`).
size_t wai_camera_report(void* ctx, char* buf, size_t n)
{
    WarContext* context = (WarContext*)ctx;
    if (!context || !context->map) return (size_t)snprintf(buf, n, "cam: no map");
    WarMap* map = context->map;
    WarInput* input = &context->input;
    return (size_t)snprintf(buf, n, "cam: viewport %d,%d | cursor %d,%d left held %d just %d | command %d",
                            (s32)map->camera.viewport.x, (s32)map->camera.viewport.y,
                            (s32)input->pos.x, (s32)input->pos.y,
                            isButtonHeld(input, WAR_MOUSE_LEFT), isButtonJustPressed(input, WAR_MOUSE_LEFT),
                            (s32)map->commandState.command.type);
}

// A one-line status for diagnostics (host `mem`, board console).
size_t wai_report(void* ctx, char* buf, size_t n)
{
    WarContext* context = (WarContext*)ctx;
    if (!wai || !wai->lv || !context->map) return (size_t)snprintf(buf, n, "ai: off");
    s32 roles[6] = {0}, workers = 0, buildings = 0, waiting = 0;
    WarEntityList* units = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_UNIT);
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        if (!wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || !wai_isComputer(context, u->player)) continue;
        WarAIUnit* info = wai_unitInfo(context, e);
        if (wai_isBuildingType(u->type)) { buildings++; continue; }
        if (wai_isWorkerType(u->type)) { workers++; continue; }
        if (!info || info->id != e->id) continue;
        s32 k = info->role == WAI_ROLE_ATTACK ? 1 : info->role == WAI_ROLE_GUARD ? 2 : info->role == WAI_ROLE_ROUTE ? 3 : info->role == WAI_ROLE_POST ? 4 : 0;
        roles[k]++;
        if (info->waiting) waiting++;
    }
#ifdef WAR_MEMDEBUG
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        if (!wai_alive(context, e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        WarAIUnit* info = u ? wai_unitInfo(context, e) : NULL;
        if (info && info->id == e->id && wai_isWorkerType(u->type))
        {
            WarStateBase* ws = wst_getActiveState(context, e);
            logInfo("worker %d at %d,%d state %d carrying %d", e->id,
                    (s32)wmap_mapToTileCoordinatesV(wu_getUnitCenterPosition(context, e)).x,
                    (s32)wmap_mapToTileCoordinatesV(wu_getUnitCenterPosition(context, e)).y,
                    ws ? (s32)ws->type : -1, wu_isCarryingResources(context, e));
        }
        if (!info || info->id != e->id || info->role != WAI_ROLE_ROUTE) continue;
        WarStateBase* st = wst_getActiveState(context, e);
        logInfo("route unit %d type %d at %d,%d waypoint %d -> %d,%d state %d idle %d",
                e->id, u->type, (s32)wmap_mapToTileCoordinatesV(wu_getUnitCenterPosition(context, e)).x, (s32)wmap_mapToTileCoordinatesV(wu_getUnitCenterPosition(context, e)).y, info->route,
                info->route >= 0 ? wai->routes[info->route + 1] : -1, info->route >= 0 ? wai->routes[info->route + 2] : -1,
                st ? (s32)st->type : -1, wst_isIdle(context, e));
    }
#endif
    s32 human = 0;
    for (s32 i = 0; i < units->count; i++)
    {
        WarEntity* e = units->items[i];
        WarUnitComponent* u = wai_alive(context, e) ? we_getUnitComponent(context, e) : NULL;
        if (u && u->player == 0) human++;
    }
#ifdef WAR_MEMDEBUG
    {
        extern u32 wpath_computes;
        static u32 last;
        printf("ai: flow fields computed %u since the last report\n", wpath_computes - last);
        last = wpath_computes;
    }
#endif
    if (wai_autoHuman)
    {
        s32 sold = 0, att = 0, wk = 0, bld = 0;
        for (s32 i = 0; i < units->count; i++)
        {
            WarEntity* e = units->items[i];
            WarUnitComponent* u = wai_alive(context, e) ? we_getUnitComponent(context, e) : NULL;
            if (!u || u->player != 0 || u->type >= WAR_UNIT_HUMAN_CORPSE) continue;
            WarAIUnit* info = wai_unitInfo(context, e);
            if (wai_isBuildingType(u->type)) bld++;
            else if (wai_isWorkerType(u->type)) wk++;
            else { sold++; if (info && info->id == e->id && info->role == WAI_ROLE_ATTACK) att++; }
        }
        printf("ai0: gold %d wood %d buildings %d workers %d soldiers %d (attacking %d) waves %d target %d,%d\n",
               context->map->players[0].gold, context->map->players[0].wood, bld, wk, sold, att, wai->waves0, wai->targetX0, wai->targetY0);
    }
    return (size_t)snprintf(buf, n, "ai: human %d | flags %#x buildings %d workers %d (gold %d) | attack %d guard %d route %d (waiting %d) post %d other %d | group %d target %d,%d wave %d",
                            human, wai->lv->flags, buildings, workers, context->map->players[1].gold,
                            roles[1], roles[2], roles[3], waiting, roles[4], roles[0], wai->groupSize,
                            wai->targetX, wai->targetY, wai->wave);
}
