#include "war_state_machine.h"

#include "TracyC.h"

WarStateGold* wst_createGatherGoldState(WarContext* context, WarEntity* entity, WarEntityId goldmineId)
{
    TracyCZoneN(ctx, "wst_createGatherGoldState", true);

    WarStateRef ref = wst_allocState(context, WAR_STATE_GOLD, entity->id);
    if (!WAR_STATE_REF_IS_VALID(ref))
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    WarStateGold* state = (WarStateGold*)wst_deref(context, ref);
    if (!state)
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    state->goldmineId = goldmineId;
#ifdef WAR_FRANK
    state->frankEntered = false;
    // (a gather command sets the work bits afresh, 0x21184)
    we_getUnitComponent(context, entity)->frankReturning = false;
#endif

    TracyCZoneEnd(ctx);
    return state;
}

void wst_updateGatherGoldState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_updateGatherGoldState", true);
#ifdef WAR_FRANK
    if (wai_drivesTicks(context) && !wos_atWorkCheckpoint(entity))
    {
        TracyCZoneEnd(ctx);
        return;
    }
#endif

    WarStateGold* s = (WarStateGold*)state;

    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    assert(unit);

    const WarUnitStats* stats = wu_getUnitStats(unit->type);

    WarEntity* goldmine = we_findEntity(context, (WarEntityId)s->goldmineId);

    // if the goldmine doesn't exists (it could ran out of gold, or other units attacking it), go idle
    // if the unit was already mining, and the gold mine ran out of gold, then another unit previouly got all the remaining gold
    // so, this unit get nothing
    if (!goldmine || wst_isCollapsing(context, goldmine) || wst_isGoingToCollapse(context, goldmine))
    {
        WarStateIdle* idleState = wst_createIdleState(context, entity, true);
        wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    // if the goldmine is not in range, go to it
    if (!wu_unitInRange(context, entity, goldmine, stats->range))
    {
#ifdef WAR_FRANK
        if (wai_drivesTicks(context))
        {
            wmv_settle(context, entity);
            TracyCZoneEnd(ctx);
            return;
        }
#endif
        WarStateFollow* followState = wst_createFollowState(context, entity, goldmine->id, VEC2_ZERO, stats->range * MEGA_TILE_WIDTH);
        wst_pushState(context, entity, (WarStateBase*)followState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

#ifdef WAR_FRANK
    // frank: the original's worker steps into the mine first
    if (!s->frankEntered)
    {
        s->frankEntered = true;
        {
            // (0x1CE5B: the walk's goal, kept to come back to)
            vec2 goal = wmv_buildingGoal(context, entity, goldmine);
            unit->frankWorkX = (s16)goal.x;
            unit->frankWorkY = (s16)goal.y;
        }
        wmv_enterBuilding(context, entity, goldmine);
        TracyCZoneEnd(ctx);
        return;
    }
#endif
    // the unit arrive to the goldmine, go mining
    WarStateMining* miningState = wst_createMiningState(context, entity, goldmine->id);
    wst_replaceState(context, entity, (WarStateBase*)miningState, WAR_TRANSITION_CAUSE_COMPLETION);

    TracyCZoneEnd(ctx);
}

void wst_updateGoldStates(WarContext* context)
{
    TracyCZoneN(ctx, "wst_updateGoldStates", true);

    WarEntityManager* manager = we_getEntityManager(context);
    WarStateStorage*  storage = &manager->stateStorage;
    WarStateGold*      states  = storage->gold;
    bool*             occupied = storage->occupied[WAR_STATE_GOLD];

    for (s32 i = 0; i < MAX_STATES_PER_TYPE; i++)
    {
        if (!occupied[i]) continue;

        WarStateGold*  state  = &states[i];
        WarEntity*    entity = we_findEntity(context, state->base.entityId);
        if (!entity) continue;

        if (wst_getActiveState(context, entity) != (WarStateBase*)state) continue;
        if (!wst_isNextUpdateTime(context, (WarStateBase*)state)) continue;

        wst_updateGatherGoldState(context, entity, (WarStateBase*)state);
    }

    TracyCZoneEnd(ctx);
}
