#include "war_state_machine.h"

#include "TracyC.h"

WarStateMining* wst_createMiningState(WarContext* context, WarEntity* entity, WarEntityId goldmineId)
{
    TracyCZoneN(ctx, "wst_createMiningState", true);

    WarStateRef ref = wst_allocState(context, WAR_STATE_MINING, entity->id);
    if (!WAR_STATE_REF_IS_VALID(ref))
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    WarStateMining* state = (WarStateMining*)wst_deref(context, ref);
    if (!state)
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    state->goldmineId = goldmineId;
    state->miningTime = 0;

    TracyCZoneEnd(ctx);
    return state;
}

void wst_enterMiningState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_enterMiningState", true);

    WarStateMining* s = (WarStateMining*)state;

    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    assert(unit);

#ifdef WAR_FRANK
    s->miningTime = WMV_INSIDE_SECONDS;
    s->frankTicks = 80;
    s->frankLoaded = false;
    if (wai_drivesTicks(context))
    {
        wos_select(context, entity, 13, 80);
        // 0x1CFF4: the load is taken on the way in, at most 100 or what is
        // left; a computer's worker at a mine the human does not see takes
        // only 5 of it from the mine. The mine is spent at once at zero.
        WarEntity* goldmine = we_findEntity(context, (WarEntityId)s->goldmineId);
        WarUnitComponent* gu = goldmine && wu_isUnit(goldmine) ? we_getUnitComponent(context, goldmine) : NULL;
        if (gu && gu->amount > 0)
        {
            s32 take = MIN(gu->amount, UNIT_MAX_CARRY_GOLD);
            WarOriginalUnit* record = wos_unit(context, goldmine);
            WarResource* level = wres_getOrCreateResource(context, context->map->levelInfoIndex);
            if (level->levelInfo.ai && level->levelInfo.ai->alliance[unit->player] == 1 &&
                record && (record->visibility & 0x100))
                take = MIN(take, 5);
            s->frankLoaded = true;
            // (the walk's goal kept to come back to, unless the mine is spent)
            unit->frankReturning = gu->amount > take;
            unit->frankWorkSource = goldmine->id;
            unit->frankWorkGold = true;
            mine(context, goldmine, take);
        }
    }
#else
    s->miningTime = 2.0f;
#endif

    we_removeSpriteComponent(context, entity);
    wact_resetAction(context, entity, unit->actionType);
    wmap_removeEntityFromSelection(context, entity->id);

    TracyCZoneEnd(ctx);
}

void wst_exitMiningState(WarContext* context, WarEntity* entity, WarState* state, WarStateExitReason reason)
{
    TracyCZoneN(ctx, "wst_exitMiningState", true);

    WarStateMining* s = (WarStateMining*)state;

    NOT_USED(context);
    NOT_USED(entity);
    NOT_USED(reason);

    WarEntity* goldmine = we_findEntity(context, (WarEntityId)s->goldmineId);

    // NOTE: if the goldmine doesn't exists (it could ran out of gold, or other units destroyed it), or it's collapsing or going to collapse,
    // restore back the sprite component to the worker, so it should be visible again
    if (!goldmine || wst_isCollapsing(context, goldmine) || wst_isGoingToCollapse(context, goldmine))
    {
        WarUnitComponent* unit = we_getUnitComponent(context, entity);
        assert(unit);

        const WarUnitData* unitData = wu_getUnitData(unit->type);
        s32 spriteIndex = unitData->resourceIndex;
        if (spriteIndex == 0)
        {
            logError("Sprite for unit of type %d is not configure properly. Default to footman sprite.", unit->type);
            spriteIndex = 279;
        }
        we_addSpriteComponentFromResource(context, entity, imageResourceRef(spriteIndex));
    }

    TracyCZoneEnd(ctx);
}

void wst_updateMiningState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_updateMiningState", true);
#ifdef WAR_FRANK
    if (wai_drivesTicks(context) && !wos_atWorkCheckpoint(entity))
    {
        TracyCZoneEnd(ctx);
        return;
    }
#endif

    WarStateMining* s = (WarStateMining*)state;

    WarMap* map = context->map;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    assert(unit);

    WarEntity* goldmine = we_findEntity(context, (WarEntityId)s->goldmineId);

    // if the goldmine doesn't exists (it could ran out of gold, or other units attacking it), or it's collapsing or going to collapse, go idle
    // if the unit was already mining, and the gold mine ran out of gold, then another unit previouly got all the remaining gold
    // so, this unit get nothing
#ifdef WAR_FRANK
    // (a worker that took its load stays its 80 ticks, the mine spent or not)
    if (wai_drivesTicks(context) && s->frankLoaded && (!goldmine ||
        wst_isCollapsing(context, goldmine) || wst_isGoingToCollapse(context, goldmine)))
    {
        if (s->frankTicks > 0) { TracyCZoneEnd(ctx); return; }
        unit->amount = UNIT_MAX_CARRY_GOLD;
        unit->resourceKind = WAR_RESOURCE_GOLD;
        vec2 tile = wu_getUnitCenterTile(context, entity);
        wu_setUnitCenterTile(context, entity, wpath_findEmptyTile(&map->finder, (s32)tile.x, (s32)tile.y));
        WarStateIdle* idleState = wst_createIdleState(context, entity, true);
        wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }
#endif
    if (!goldmine)
    {
        vec2 tile = wu_getUnitCenterTile(context, entity);
        vec2 spawnTile = wpath_findEmptyTile(&map->finder, (s32)tile.x, (s32)tile.y);
        wu_setUnitCenterTile(context, entity, spawnTile);

        WarStateIdle* idleState = wst_createIdleState(context, entity, true);
        wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    if (wst_isCollapsing(context, goldmine) || wst_isGoingToCollapse(context, goldmine))
    {
        // find a valid spawn position for the unit
        vec2 tile = wu_getUnitCenterTile(context, goldmine);
        vec2 spawnTile = wpath_findEmptyTile(&map->finder, (s32)tile.x, (s32)tile.y);
        wu_setUnitCenterTile(context, entity, spawnTile);

        WarStateIdle* idleState = wst_createIdleState(context, entity, true);
        wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

#ifdef WAR_FRANK
    // the AI's tick counts the 80 ticks (wst_frankInsideTick)
    if (wai_drivesTicks(context)) s->miningTime = s->frankTicks > 0 ? 1.0f : -1.0f;
    else
#endif
    s->miningTime -= context->gameDeltaTime;

    if (s->miningTime < 0)
    {
#ifdef WAR_FRANK
        // (taken on the way in; a load is a delivery of 100, 0x1CF54)
        if (wai_drivesTicks(context)) unit->amount = s->frankLoaded ? UNIT_MAX_CARRY_GOLD : unit->amount;
        else
#endif
        unit->amount += mine(context, goldmine, UNIT_MAX_CARRY_GOLD);
        if (unit->amount > 0)
        {
            unit->resourceKind = WAR_RESOURCE_GOLD;
        }

#ifdef WAR_FRANK
        {
            // frank: out on the side towards the hall it carries the gold to
            // (0x2E554)
#ifdef WAR_FRANK
            WarEntity* hall = wai_homeHall(context, entity);
#else
            WarEntity* hall = we_findClosestUnitOfType(context, entity, wu_getTownHallOfRace(wu_getUnitRace(context, entity)));
#endif
            WarUnitComponent* hu = hall ? we_getUnitComponent(context, hall) : NULL;
            WarUnitComponent* gu = we_getUnitComponent(context, goldmine);
            s32 tx = hu ? hu->tilex : gu->tilex, ty = hu ? hu->tiley : gu->tiley;
            if (!(unit->amount > 0)) { tx = gu->tilex; ty = gu->tiley; }
            wu_setUnitCenterTile(context, entity, wmv_exitTile(context, goldmine, tx, ty));
        }
#else
        // find a valid spawn position for the unit
        vec2 tile = wu_getUnitCenterTile(context, goldmine);
        vec2 spawnTile = wpath_findEmptyTile(&map->finder, (s32)tile.x, (s32)tile.y);
        wu_setUnitCenterTile(context, entity, spawnTile);
#endif

        // set the carrying gold sprites
        const WarWorkerData* workerData = wu_getWorkerData(unit->type);
        we_addSpriteComponentFromResource(context, entity, imageResourceRef(workerData->carryingGoldResource));
#ifdef WAR_FRANK
        wmv_occupy(context, entity);   // (0x13928, on the way out)
        if (wai_drivesTicks(context)) wos_select(context, entity, 2, 1);
#endif

        // find the closest town hall to deliver the gold
        WarRace race = wu_getUnitRace(context, entity);
        WarUnitType townHallType = wu_getTownHallOfRace(race);
#ifdef WAR_FRANK
        WarEntity* townHall = wai_homeHall(context, entity);
#else
        WarEntity* townHall = we_findClosestUnitOfType(context, entity, townHallType);
#endif

        // if the town hall doesn't exists (it could be under attack and get destroyed), go idle
        if (!townHall)
        {
            WarStateIdle* idleState = wst_createIdleState(context, entity, true);
            wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
            TracyCZoneEnd(ctx);
            return;
        }

        WarStateDeliver* deliverState = wst_createDeliverState(context, entity, townHall->id);
        deliverState->cycle = true;
        deliverState->sourceKind = WAR_RESOURCE_GOLD;
        deliverState->sourceId = goldmine->id;
        wst_replaceState(context, entity, (WarStateBase*)deliverState, WAR_TRANSITION_CAUSE_COMPLETION);
    }

    TracyCZoneEnd(ctx);
}

void wst_updateMiningStates(WarContext* context)
{
    TracyCZoneN(ctx, "wst_updateMiningStates", true);

    WarEntityManager* manager = we_getEntityManager(context);
    WarStateStorage*  storage = &manager->stateStorage;
    WarStateMining*      states  = storage->mining;
    bool*             occupied = storage->occupied[WAR_STATE_MINING];

    for (s32 i = 0; i < MAX_STATES_PER_TYPE; i++)
    {
        if (!occupied[i]) continue;

        WarStateMining*  state  = &states[i];
        WarEntity*    entity = we_findEntity(context, state->base.entityId);
        if (!entity) continue;

        if (wst_getActiveState(context, entity) != (WarStateBase*)state) continue;
        if (!wst_isNextUpdateTime(context, (WarStateBase*)state)) continue;

        wst_updateMiningState(context, entity, (WarStateBase*)state);
    }

    TracyCZoneEnd(ctx);
}
