#include "war_state_machine.h"

#include "TracyC.h"

WarStateDeliver* wst_createDeliverState(WarContext* context, WarEntity* entity, WarEntityId townHallId)
{
    TracyCZoneN(ctx, "wst_createDeliverState", true);

    WarStateRef ref = wst_allocState(context, WAR_STATE_DELIVER, entity->id);
    if (!WAR_STATE_REF_IS_VALID(ref))
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    WarStateDeliver* state = (WarStateDeliver*)wst_deref(context, ref);
    if (!state)
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    state->townHallId = townHallId;
    state->insideBuilding = false;
#ifdef WAR_FRANK
    state->frankEntered = false;
#endif
    state->cycle = false;
    state->sourceKind = WAR_RESOURCE_NONE;
    state->sourceId = 0;
    state->sourcePosition = VEC2_ZERO;

    TracyCZoneEnd(ctx);
    return state;
}

void wst_updateDeliverState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_updateDeliverState", true);
#ifdef WAR_FRANK
    if (wai_drivesTicks(context) && !wos_atWorkCheckpoint(entity))
    {
        TracyCZoneEnd(ctx);
        return;
    }
#endif

    WarStateDeliver* s = (WarStateDeliver*)state;

    WarStateMachineComponent* sm = we_getStateMachineComponent(context, entity);
    assert(sm);

    WarMap* map = context->map;

    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    assert(unit);

    const WarUnitStats* stats = wu_getUnitStats(unit->type);

    WarEntity* townHall = we_findEntity(context, (WarEntityId)s->townHallId);

    // if the town hall doesn't exists (or other units attacking it), go idle
    if (!townHall)
    {
        WarStateIdle* idleState = wst_createIdleState(context, entity, true);
        wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    if (!wu_unitInRange(context, entity, townHall, stats->range))
    {
#ifdef WAR_FRANK
        if (wai_drivesTicks(context))
        {
            wmv_settle(context, entity);
            TracyCZoneEnd(ctx);
            return;
        }
#endif
        vec2 targetPosition = wu_unitPointOnTarget(context, entity, townHall);
        WarStateFollow* followState = wst_createFollowState(context, entity, townHall->id, targetPosition, stats->range * MEGA_TILE_WIDTH);
        wst_pushState(context, entity, (WarStateBase*)followState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    if (s->insideBuilding)
    {
#ifdef WAR_FRANK
        {
            // frank: out on the side towards where it works (0x2E554)
            WarUnitComponent* hu = we_getUnitComponent(context, townHall);
            s32 tx = hu->tilex, ty = hu->tiley;
            WarEntity* src = s->cycle && s->sourceKind == WAR_RESOURCE_GOLD ? we_findEntity(context, s->sourceId) : NULL;
            WarUnitComponent* su = src && wu_isUnit(src) ? we_getUnitComponent(context, src) : NULL;
            if (su) { tx = unit->frankWorkX; ty = unit->frankWorkY; }
            else if (s->cycle && s->sourceKind == WAR_RESOURCE_WOOD)
            {
                vec2 t = wmap_mapToTileCoordinatesV(s->sourcePosition);
                tx = (s32)t.x; ty = (s32)t.y;
            }
            wu_setUnitCenterTile(context, entity, wmv_exitTile(context, townHall, tx, ty));
        }
#else
        // find a valid spawn position for the unit
        vec2 tile = wu_getUnitCenterTile(context, townHall);
        vec2 spawnTile = wpath_findEmptyTile(&map->finder, (s32)tile.x, (s32)tile.y);
        wu_setUnitCenterTile(context, entity, spawnTile);
#endif

        const WarUnitData* unitData = wu_getUnitData(unit->type);
        we_removeSpriteComponent(context, entity);
        we_addSpriteComponentFromResource(context, entity, imageResourceRef(unitData->resourceIndex));
#ifdef WAR_FRANK
        wmv_occupy(context, entity);   // (0x13928, on the way out)
#endif

#ifdef WAR_FRANK
        if (wai_drivesTicks(context)) wos_select(context, entity, 2, 1);
        // frank: out of the hall a computer's worker thinks again (0x1D1A0
        // calls 0x2B154); the human's goes back to its work
        if (s->cycle && wai_workerExit(context, entity))
        {
            TracyCZoneEnd(ctx);
            return;
        }
#endif
        if (s->cycle)
        {
            if (s->sourceKind == WAR_RESOURCE_GOLD)
            {
                WarStateGold* gatherGoldState = wst_createGatherGoldState(context, entity, s->sourceId);
                wst_replaceState(context, entity, (WarStateBase*)gatherGoldState, WAR_TRANSITION_CAUSE_COMPLETION);
            }
            else
            {
                WarStateWood* gatherWoodState = wst_createGatherWoodState(context, entity, s->sourceId, s->sourcePosition);
                wst_replaceState(context, entity, (WarStateBase*)gatherWoodState, WAR_TRANSITION_CAUSE_COMPLETION);
            }
        }
        else if (sm->depth > 1)
        {
            wst_popState(context, entity, WAR_TRANSITION_CAUSE_COMPLETION, WAR_STATE_RESULT_SUCCESS);
        }
        else
        {
            WarStateIdle* idleState = wst_createIdleState(context, entity, true);
            wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
        }

        TracyCZoneEnd(ctx);
        return;
    }

#ifdef WAR_FRANK
    // frank: the original's worker steps into the hall first
    if (!s->frankEntered)
    {
        s->frankEntered = true;
        wmv_enterBuilding(context, entity, townHall);
        TracyCZoneEnd(ctx);
        return;
    }
#endif
    if (unit->resourceKind == WAR_RESOURCE_GOLD)
    {
        WarPlayerInfo* player = wu_getOwningPlayer(context, entity);
        we_increasePlayerResources(context, player, unit->amount, 0);
    }
    else if (unit->resourceKind == WAR_RESOURCE_WOOD)
    {
        WarPlayerInfo* player = wu_getOwningPlayer(context, entity);
        we_increasePlayerResources(context, player, 0, unit->amount);
    }

    unit->resourceKind = WAR_RESOURCE_NONE;
    unit->amount = 0;

    // the unit arrive to the townhall, so now the unit go inside the building for some time to simulate the depositing
    // then need go back to the goldmine/trees.
    s->insideBuilding = true;

    we_disableComponent(context, entity, COMP_SPRITE);

    // remove the unit from selection to avoid the player giving it orders
    // while inside the townhall
    wmap_removeEntityFromSelection(context, entity->id);

    // simulate the time inside the townhall
#ifdef WAR_FRANK
    state->delay = WMV_INSIDE_SECONDS;
    s->frankTicks = 80;
    if (wai_drivesTicks(context)) state->delay = 1000.0f;   // (the AI's tick lets it out)
    if (wai_drivesTicks(context)) wos_select(context, entity, 13, 80);
#else
    state->delay = wmap_getMapScaledTime(context, 1.0f);
#endif

    TracyCZoneEnd(ctx);
}


void wst_updateDeliverStates(WarContext* context)
{
    TracyCZoneN(ctx, "wst_updateDeliverStates", true);

    WarEntityManager* manager = we_getEntityManager(context);
    WarStateStorage*  storage = &manager->stateStorage;
    WarStateDeliver*      states  = storage->deliver;
    bool*             occupied = storage->occupied[WAR_STATE_DELIVER];

    for (s32 i = 0; i < MAX_STATES_PER_TYPE; i++)
    {
        if (!occupied[i]) continue;

        WarStateDeliver*  state  = &states[i];
        WarEntity*    entity = we_findEntity(context, state->base.entityId);
        if (!entity) continue;

        if (wst_getActiveState(context, entity) != (WarStateBase*)state) continue;
        if (!wst_isNextUpdateTime(context, (WarStateBase*)state)) continue;

        wst_updateDeliverState(context, entity, (WarStateBase*)state);
    }

    TracyCZoneEnd(ctx);
}

#ifdef WAR_FRANK
// frank: a worker's 80 ticks inside a mine or a hall, in its turn of the
// unit table; it comes out on the tick (the original's order 13, 0x1D1A0).
void wst_frankInsideTick(WarContext* context, WarEntity* entity, WarStateBase* st)
{
    if (!st) return;
    if (st->type == WAR_STATE_MINING)
    {
        WarStateMining* s = (WarStateMining*)st;
        if (wos_tick(context, entity, NULL) == WOR_CHANGED)
        {
            s->frankTicks = 0;
            wos_workHandler(context, entity, st);
            wmv_settle(context, entity);   // (the next order at once: the walk to the hall)
        }
    }
    else if (st->type == WAR_STATE_DELIVER)
    {
        WarStateDeliver* s = (WarStateDeliver*)st;
        if (s->insideBuilding && wos_tick(context, entity, NULL) == WOR_CHANGED)
        {
            s->frankTicks = 0;
            st->delay = 0;
            st->nextUpdateGameTime = 0;
            wos_workHandler(context, entity, st);
            wmv_settle(context, entity);
        }
    }
}
#endif
