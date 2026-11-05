// Headless integration fixtures on the real game, resources and script dispatcher.
// SPDX-License-Identifier: Zlib
#define wai_debug wai_gameDebug
#include "../third_party/war1/src/war1.c"
#undef wai_debug

static WarEntityId spellCaster, spellRecipient;
static u32 spellStarted;
static s32 spellOrder, spellType, spellHealth;
static WarEntityId corpseVictims[3];
static u16 corpseSlots[3];

static s32 summonCount(WarContext* context, s32 type)
{
    return we_getUnitsOfType(context, wu_fromOriginalType(type))->count;
}

static WarEntity* spellUnitAt(WarContext* context, WarUnitType type, s32 x, s32 y)
{
    WarEntity* entity = we_createUnit(context, CREATE_UNIT_ARGS_INIT(
        .type = type, .player = 0, .x = x, .y = y, .addToMap = true));
    assert(entity);
    we_setInitialIdleState(context, entity);
    wst_processPendingTransitionOf(context, entity);
    return entity;
}

static WarEntity* spellUnit(WarContext* context, WarUnitType type)
{
    WarEntity* hall = wai_townHall(context, 0);
    assert(hall);
    vec2 tile = wu_getUnitCenterTile(context, hall), exit;
    assert(wmv_findExit(context, hall, (s32)tile.x, (s32)tile.y, &exit));
    return spellUnitAt(context, type, (s32)exit.x, (s32)exit.y);
}

void wai_debug(void* opaque, const char* command)
{
    WarContext* context = opaque;
    int order, type, health;
    if (!strcmp(command, "test-no-path"))
    {
        WarEntity* walker = spellUnit(context, WAR_UNIT_FOOTMAN);
        vec2 here = wu_getUnitCenterTile(context, walker);
        WarEntity* blocker = NULL;
        for (unsigned direction = 0; direction < 8 && !blocker; direction++)
        {
            s32 x = (s32)here.x + wmv_dx[direction], y = (s32)here.y + wmv_dy[direction];
            if (wmv_placeFree(context, WAR_UNIT_FOOTMAN, x, y))
                blocker = spellUnitAt(context, WAR_UNIT_FOOTMAN, x, y);
        }
        assert(blocker);
        WarStateMove* move = wst_createMoveState(context, walker, 2,
            arrayArg(vec2, wu_getUnitCenterPosition(context, walker), wu_getUnitCenterPosition(context, blocker)), false);
        assert(move);
        move->frankRange = 0;
        move->frankNextOrder = WOR_NONE;
        assert(wst_resetState(context, walker, (WarStateBase*)move, WAR_TRANSITION_CAUSE_PLAYER_ORDER));
        wst_processPendingTransitionOf(context, walker);
        wmv_tickBegin(context);
        assert(wmv_walkingOn(context, walker));
        assert(!wmv_checkpoint(context, walker, we_getUnitComponent(context, walker), move));
        assert(wst_getActiveState(context, walker)->type == WAR_STATE_IDLE);
        assert(!wmv_walkingOn(context, walker));
        assert(wos_unit(context, walker)->current == 2);
        printf("live spell movement: failed route is stationary before the next unit plans\n");
        return;
    }
    if (!strcmp(command, "test-wall-bits"))
    {
        // a wall piece's terrain bit is its owner's, 4 or 8 (0x51DC0)
        WarEntity* wall = context->map->editing.wall;
        vec2 hall = wu_getUnitCenterTile(context, wai_townHall(context, 0));
        s32 tiles[2][2], found = 0;
        for (s32 y = (s32)hall.y + 3; found < 2 && y < MIN(63, (s32)hall.y + 12); y++)
            for (s32 x = (s32)hall.x + 3; found < 2 && x < MIN(63, (s32)hall.x + 12); x++)
                if (wmv_placeFree(context, WAR_UNIT_FOOTMAN, x, y) && !wmv_hasTerrain(x, y, 0xC))
                {
                    tiles[found][0] = x; tiles[found][1] = y; found++;
                    x += 2;
                }
        assert(wall && found == 2);
        for (s32 i = 0; i < 2; i++)
        {
            WarWallPiece* piece = we_addWallPiece(context, wall, tiles[i][0], tiles[i][1], i);
            piece->hp = piece->maxhp = WAR_WALL_MAX_HP;
        }
        wmv_tickBegin(context);
        assert(wmv_hasTerrain(tiles[0][0], tiles[0][1], 4) && !wmv_hasTerrain(tiles[0][0], tiles[0][1], 8));
        assert(wmv_hasTerrain(tiles[1][0], tiles[1][1], 8) && !wmv_hasTerrain(tiles[1][0], tiles[1][1], 4));
        assert(!wmv_placeFree(context, WAR_UNIT_FOOTMAN, tiles[0][0], tiles[0][1]));
        for (s32 i = 0; i < 2; i++)
        {
            const s32 t = tiles[i][1] * WMV_W + tiles[i][0];
            assert(wmv_visual[t] >> 11 == 20 && (wmv_visual[t] & 0x7FF) >= 10);
        }
        // worn down in the terrain (0x105D0), the piece goes with its bit
        WarWallPiece* piece = we_getWallPieceAtPosition(context, wall, tiles[1][0], tiles[1][1]);
        s32 strikes = 0;
        while (wmv_hasTerrain(tiles[1][0], tiles[1][1], 0xC) && strikes < 200)
        {
            we_takeWallDamage(context, wall, piece, 40, 0);
            strikes++;
        }
        assert(strikes > 1 && strikes < 200 && piece->hp == 0);
        assert(wmv_hasTerrain(tiles[0][0], tiles[0][1], 4));
        printf("live spell terrain: walls laid with owner bit and durability, worn away in %d strikes\n", strikes);
        return;
    }
    if (!strcmp(command, "test-occupy"))
    {
        // a unit put on the map holds its tile within the same tick (0x13928)
        WarEntity* hall = wai_townHall(context, 0);
        vec2 tile = wu_getUnitCenterTile(context, hall), exit;
        wmv_tickBegin(context);
        assert(wmv_findExit(context, hall, (s32)tile.x, (s32)tile.y, &exit));
        WarEntity* made = we_createUnit(context, CREATE_UNIT_ARGS_INIT(
            .type = WAR_UNIT_FOOTMAN, .player = 0, .x = (s32)exit.x, .y = (s32)exit.y, .addToMap = true));
        assert(made);
        we_setInitialIdleState(context, made);
        assert(!wmv_exitBlocked(NULL, (s16)exit.x, (s16)exit.y));
        wmv_occupy(context, made);
        assert(wmv_exitBlocked(NULL, (s16)exit.x, (s16)exit.y));
        vec2 next;
        assert(wmv_findExit(context, hall, (s32)tile.x, (s32)tile.y, &next));
        assert(next.x != exit.x || next.y != exit.y);
        printf("live spell movement: a placed unit holds its tile before later units move\n");
        return;
    }
    if (!strcmp(command, "test-poison"))
    {
        WarEntity* caster = spellUnit(context, WAR_UNIT_WARLOCK);
        spellCaster = caster->id;
        spellStarted = wai->ticks;
        spellHealth = we_getUnitComponent(context, caster)->hp;
        we_getUnitComponent(context, caster)->mana = 50;
        WarEntityIdListClear(&context->map->selectedEntities);
        WarEntityIdListAdd(&context->map->selectedEntities, caster->id);
        wcmd_executePoisonCloudCommand(context, wu_getUnitCenterPosition(context, caster));
        assert(we_getUnitComponent(context, caster)->mana == 50);
        return;
    }
    if (!strcmp(command, "check-poison"))
    {
        WarEntity* caster = we_findEntity(context, spellCaster);
        assert(caster);
        WarUnitComponent* unit = we_getUnitComponent(context, caster);
        unsigned clouds = 0;
        for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++)
            if (wft_missiles[i].type == 3 && wft_missiles[i].source == unit->frankOriginalSlot)
            {
                assert(wft_missiles[i].pulses < 25);
                clouds++;
            }
        assert(clouds == 1 && unit->hp == spellHealth);
        assert(unit->mana <= (s32)(wai->ticks - spellStarted) / 40 + 1);
        assert(wos_unit(context, caster)->current == 2);
        assert(we_getEntitiesOfType(context, WAR_ENTITY_TYPE_POISON_CLOUD)->count == 0);
        printf("live spell 27: deferred fifty-mana cloud, original script pulses and caster immunity\n");
        return;
    }
    if (!strcmp(command, "test-replace-ai"))
    {
        WarEntity* walker = we_findEntity(context, spellCaster);
        assert(walker);
        const vec2 here = wu_getUnitCenterPosition(context, walker);
        WarStateMove* first = wst_createMoveState(context, walker, 2,
            arrayArg(vec2, here, vec2_addv(here, vec2i(16, 0))), false);
        assert(first);
        const WarStateRef firstRef = wst_refOf(context, (WarStateBase*)first);
        assert(wai_order(context, walker, (WarStateBase*)first));
        WarStateMove* second = wst_createMoveState(context, walker, 2,
            arrayArg(vec2, here, vec2_addv(here, vec2i(32, 0))), false);
        assert(second && wai_order(context, walker, (WarStateBase*)second));
        assert(!wst_deref(context, firstRef));
        WarStateMachineComponent* sm = we_getStateMachineComponent(context, walker);
        assert(wst_deref(context, sm->pending.stateRef) == (WarStateBase*)second);
        assert(we_getUnitComponent(context, walker)->frankOrderQueued);
        assert(wst_getActiveState(context, walker)->type == WAR_STATE_IDLE);
        WarStateIdle* playerOrder = wst_createIdleState(context, walker, false);
        assert(playerOrder && wst_resetState(context, walker, (WarStateBase*)playerOrder, WAR_TRANSITION_CAUSE_PLAYER_ORDER));
        WarStateIdle* aiOrder = wst_createIdleState(context, walker, false);
        assert(aiOrder && !wai_order(context, walker, (WarStateBase*)aiOrder));
        assert(wst_deref(context, sm->pending.stateRef) == (WarStateBase*)playerOrder);
        printf("live spell command: later AI redirect replaces its predecessor, preserving player priority\n");
        return;
    }
    if (!strcmp(command, "test-corpses"))
    {
        vec2 hall = wu_getUnitCenterTile(context, wai_townHall(context, 0));
        bool placed = false;
        for (s32 y = (s32)hall.y + 1; !placed && y < MIN(63, (s32)hall.y + 10); y++)
            for (s32 x = (s32)hall.x + 1; !placed && x < MIN(60, (s32)hall.x + 10); x++)
            {
                bool free = true;
                for (s32 i = 0; i < 4; i++)
                    if (!wmv_placeFree(context, WAR_UNIT_FOOTMAN, x + i, y)) free = false;
                if (!free) continue;
                spellCaster = spellUnitAt(context, WAR_UNIT_NECROLYTE, x, y)->id;
                for (s32 i = 0; i < 3; i++)
                {
                    WarEntity* victim = spellUnitAt(context, WAR_UNIT_FOOTMAN, x + i + 1, y);
                    corpseVictims[i] = victim->id;
                    corpseSlots[i] = we_getUnitComponent(context, victim)->frankOriginalSlot - 1;
                }
                placed = true;
            }
        assert(placed);
        return;
    }
    if (!strcmp(command, "kill-corpses"))
    {
        assert(we_findEntity(context, spellCaster));
        for (unsigned i = 0; i < 3; i++)
        {
            WarEntity* victim = we_findEntity(context, corpseVictims[i]);
            assert(victim);
            we_takeDamage(context, victim, 1000, 0);
            wst_processPendingTransitionOf(context, victim);
        }
        return;
    }
    if (!strcmp(command, "test-resurrect"))
    {
        WarEntity* caster = we_findEntity(context, spellCaster);
        assert(caster);
        WarEntityManager* manager = we_getEntityManager(context);
        for (unsigned i = 0; i < 3; i++)
        {
            const WarOriginalUnit* corpse = &manager->originalUnits[corpseSlots[i]];
            if (corpse->type != 51 || !(corpse->flags & WOR_DYING) || (corpse->flags & WOR_INSIDE))
                fprintf(stderr, "corpse fixture %u slot %u: type %u flags %x order %u pc %u wait %u\n",
                    i, corpseSlots[i], corpse->type, corpse->flags, corpse->current, corpse->pc, corpse->wait);
            assert(corpse->type == 51 && (corpse->flags & WOR_DYING) && !(corpse->flags & WOR_INSIDE));
        }
        const WarOriginalUnit* blocked = &manager->originalUnits[corpseSlots[0]];
        WarEntity* blocker = spellUnitAt(context, WAR_UNIT_FOOTMAN, blocked->tileX, blocked->tileY);
        we_getUnitComponent(context, caster)->mana = 70;
        const s32 before = summonCount(context, 26);
        assert(wos_resurrect(context, caster, true) == WOR_CHANGED);
        assert(we_getUnitComponent(context, caster)->mana == 0 && summonCount(context, 26) == before + 1);
        assert(!(manager->originalUnits[corpseSlots[0]].flags & WOR_INSIDE));
        assert(manager->originalUnits[corpseSlots[1]].flags & WOR_INSIDE);
        const WarOriginalUnit* raised = &manager->originalUnits[corpseSlots[1]];
        bool facingMatched = false;
        for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++)
        {
            WarEntity* skeleton = manager->originalEntities[slot];
            if (!skeleton || !wu_isSkeletonUnit(context, skeleton)) continue;
            vec2 tile = wu_getUnitCenterTile(context, skeleton);
            if (tile.x == raised->tileX && tile.y == raised->tileY)
                facingMatched = wu_getUnitDirection(context, skeleton) == raised->direction;
        }
        assert(facingMatched);
        wpath_setFreeTiles(&context->map->finder, blocked->tileX, blocked->tileY, 1, 1);
        we_removeEntityById(context, blocker->id);
        we_getUnitComponent(context, caster)->mana = 140;
        spellStarted = wai->ticks;
        spellOrder = 20;
        spellHealth = summonCount(context, 26);
        assert(wai_raiseDead(context, caster));
        assert(we_getUnitComponent(context, caster)->mana == 140 && summonCount(context, 26) == spellHealth);
        return;
    }
    if (sscanf(command, "test-summon %d", &order) == 1)
    {
        assert(order >= 22 && order <= 25);
        WarEntity* caster = spellUnit(context, order == 23 || order == 24 ? WAR_UNIT_WARLOCK : WAR_UNIT_CONJURER);
        we_getUnitComponent(context, caster)->mana = 255;
        spellCaster = caster->id;
        spellStarted = wai->ticks;
        spellOrder = order;
        spellType = order == 22 ? 31 : order == 23 ? 28 : order == 24 ? 21 : 24;
        spellHealth = summonCount(context, spellType);
        WarEntityIdListClear(&context->map->selectedEntities);
        WarEntityIdListAdd(&context->map->selectedEntities, caster->id);
        wcmd_executeSummonCommand(context, order == 22 ? WAR_COMMAND_SUMMON_WATER_ELEMENTAL :
            order == 23 ? WAR_COMMAND_SUMMON_DAEMON : order == 24 ? WAR_COMMAND_SUMMON_SPIDER : WAR_COMMAND_SUMMON_SCORPION);
        assert(we_getUnitComponent(context, caster)->mana == 255);
        assert(summonCount(context, spellType) == spellHealth);
        return;
    }
    if (sscanf(command, "test-spell %d %d %d", &order, &type, &health) == 3)
    {
        assert(order == 17 || order == 21);
        WarEntity* caster = spellUnit(context, order == 17 ? WAR_UNIT_CLERIC : WAR_UNIT_NECROLYTE);
        WarEntity* recipient = spellUnit(context, wu_fromOriginalType(type));
        we_getUnitComponent(context, caster)->mana = 200;
        we_getUnitComponent(context, recipient)->hp = health;
        spellCaster = caster->id;
        spellRecipient = recipient->id;
        spellStarted = wai->ticks;
        spellOrder = order;
        spellType = type;
        spellHealth = health;
        WarStateCast* cast = wst_createCastState(context, caster,
            order == 17 ? WAR_SPELL_HEALING : WAR_SPELL_UNHOLY_ARMOR,
            recipient->id, wu_getUnitCenterPosition(context, recipient));
        assert(cast && wst_resetState(context, caster, (WarStateBase*)cast, WAR_TRANSITION_CAUSE_PLAYER_ORDER));
        assert(we_getUnitComponent(context, caster)->mana == 200);
        assert(we_getUnitComponent(context, recipient)->hp == health);
        return;
    }
    if (!strcmp(command, "check-spell"))
    {
        WarEntity* caster = we_findEntity(context, spellCaster);
        assert(caster);
        if (spellOrder == 20)
        {
            assert(summonCount(context, 26) == spellHealth + 2);
            WarEntityManager* manager = we_getEntityManager(context);
            for (unsigned i = 0; i < 3; i++)
                assert(manager->originalUnits[corpseSlots[i]].flags & WOR_INSIDE);
            assert(we_getUnitComponent(context, caster)->mana <= (s32)(wai->ticks - spellStarted) / 40 + 1);
            assert(wos_unit(context, caster)->current == 2);
            printf("live spell 20: blocked corpse preserved, facing retained, two deferred resurrections\n");
            return;
        }
        if (spellOrder >= 22)
        {
            const s32 count = spellOrder >= 24 ? 4 : 1;
            const s32 balance = spellOrder >= 24 ? 15 : 0;
            WarUnitComponent* unit = we_getUnitComponent(context, caster);
            assert(summonCount(context, spellType) == spellHealth + count);
            assert(unit->mana >= balance && unit->mana <= balance + (s32)(wai->ticks - spellStarted) / 40);
            assert(wos_unit(context, caster)->current == 2);
            printf("live spell %d: deferred player command created %d summons\n", spellOrder, count);
            return;
        }
        WarEntity* recipient = we_findEntity(context, spellRecipient);
        assert(recipient);
        WarUnitComponent* cu = we_getUnitComponent(context, caster);
        WarUnitComponent* ru = we_getUnitComponent(context, recipient);
        const s32 elapsed = (s32)(wai->ticks - spellStarted);
        s32 cost, expectedHealth;
        if (spellOrder == 17)
        {
            const s32 change = MIN(40, spellType == 28 ? spellHealth : wu_getUnitStats(ru->type)->hp - spellHealth);
            cost = change * 3;
            expectedHealth = spellType == 28 ? spellHealth - change : spellHealth + change;
        }
        else
        {
            cost = 125;
            expectedHealth = spellHealth <= 30 ? 1 : spellHealth / 2;
            assert(ru->invulnerable && wos_unit(context, recipient)->invulnerability > 1000 - elapsed);
            assert(wos_unit(context, recipient)->invulnerability < 1000);
        }
        assert(ru->hp == expectedHealth);
        assert(cu->mana == 200 + elapsed / 40 - cost);
        assert(wos_unit(context, caster)->current == 2);
        assert(!(wos_unit(context, recipient)->flags & WOR_INACTIVE));
        printf("live spell %d type %d: HP %d -> %d, mana cost %d, elapsed %d\n",
               spellOrder, spellType, spellHealth, ru->hp, cost, elapsed);
        return;
    }
    wai_gameDebug(opaque, command);
}
