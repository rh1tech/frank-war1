#include "war_state_machine.h"

#include "TracyC.h"

WarStateWood* wst_createGatherWoodState(WarContext* context, WarEntity* entity, WarEntityId forestId, vec2 position)
{
    TracyCZoneN(ctx, "wst_createGatherWoodState", true);

    WarStateRef ref = wst_allocState(context, WAR_STATE_WOOD, entity->id);
    if (!WAR_STATE_REF_IS_VALID(ref))
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    WarStateWood* state = (WarStateWood*)wst_deref(context, ref);
    if (!state)
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    state->forestId = forestId;
    state->position = position;
#ifdef WAR_FRANK
    state->frankMoved = false;
#endif

    TracyCZoneEnd(ctx);
    return state;
}

void wst_updateGatherWoodState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_updateGatherWoodState", true);
#ifdef WAR_FRANK
    if (wai_drivesTicks(context) && !wos_atWorkCheckpoint(entity))
    {
        TracyCZoneEnd(ctx);
        return;
    }
#endif

    WarStateWood* s = (WarStateWood*)state;

    WarMap* map = context->map;

    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    assert(unit);

    const WarUnitStats* stats = wu_getUnitStats(unit->type);

    WarEntity* forest = we_findEntity(context, (WarEntityId)s->forestId);
    if (!forest)
    {
        WarStateIdle* idleState = wst_createIdleState(context, entity, true);
        wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    vec2 treePosition = s->position;
    vec2 treeTile = wmap_mapToTileCoordinatesV(treePosition);
    WarTree* tree = we_getTreeAtTile(context, forest, (s32)treeTile.x, (s32)treeTile.y);
#ifdef WAR_FRANK
    {
        // frank: the original's wood order (0x21184, 0x1CD50): the walk goes
        // to the first thing in the way on the line to the tree (0x224AC),
        // the forest's edge; there it chops that tree, or with none, the
        // nearest one round it (0x2BCD4), reachable or not
        vec2 here = wu_getUnitCenterTile(context, entity);
        const s32 ux = (s32)here.x, uy = (s32)here.y;
        WarOriginalUnit* original = wos_unit(context, entity);
        vec2 edge = wai_drivesTicks(context) && original ? vec2i(original->goalX, original->goalY)
            : wmv_backOff(context, entity, (s32)treeTile.x, (s32)treeTile.y);
        const s32 ex = (s32)edge.x, ey = (s32)edge.y;
        if (MAX(ABS(ux - ex), ABS(uy - ey)) > 1)
        {
            if (wai_drivesTicks(context))
            {
                wmv_settle(context, entity);
                TracyCZoneEnd(ctx);
                return;
            }
            if (s->frankMoved)
            {
                // the walk did not get there: the order is over
                wst_replaceState(context, entity, (WarStateBase*)wst_createIdleState(context, entity, true), WAR_TRANSITION_CAUSE_COMPLETION);
                TracyCZoneEnd(ctx);
                return;
            }
            s->frankMoved = true;
            vec2 position = wu_getUnitCenterPosition(context, entity);
            WarStateMove* moveState = wst_createMoveState(context, entity, 2, arrayArg(vec2, position, wmap_tileToMapCoordinatesV(edge, true)), false);
            if (moveState) moveState->frankRange = 1;
            wst_pushState(context, entity, (WarStateBase*)moveState, WAR_TRANSITION_CAUSE_COMPLETION);
            TracyCZoneEnd(ctx);
            return;
        }
        s32 tx = ex, ty = ey;
        const bool standing = wmv_standingTree(context, tx, ty);
        if (!standing && !wmv_treeAround(context, ux, uy, &tx, &ty))
        {
            wst_replaceState(context, entity, (WarStateBase*)wst_createIdleState(context, entity, true), WAR_TRANSITION_CAUSE_COMPLETION);
            TracyCZoneEnd(ctx);
            return;
        }
        if (!standing && wai_drivesTicks(context))
        {
            s->position = wmap_tileToMapCoordinatesV(vec2i(tx, ty), true);
            s->frankMoved = false;
            wmv_settle(context, entity);
            TracyCZoneEnd(ctx);
            return;
        }
        WarEntity* treeForest = we_getForestAtTile(context, tx, ty);
        if (!treeForest)
        {
            wst_replaceState(context, entity, (WarStateBase*)wst_createIdleState(context, entity, true), WAR_TRANSITION_CAUSE_COMPLETION);
            TracyCZoneEnd(ctx);
            return;
        }
        if (MAX(ABS(ux - tx), ABS(uy - ty)) > 1)
        {
            // a tree further off: a new wood order to it
            s->forestId = treeForest->id;
            s->position = wmap_tileToMapCoordinatesV(vec2i(tx, ty), true);
            s->frankMoved = false;
            TracyCZoneEnd(ctx);
            return;
        }
        WarStateChopping* chop = wst_createChoppingState(context, entity, treeForest->id, wmap_tileToMapCoordinatesV(vec2i(tx, ty), true));
        wst_replaceState(context, entity, (WarStateBase*)chop, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }
#endif

    if (!tree || tree->amount == 0 || !wpath_isTileAccesible(&map->finder, (s32)treeTile.x, (s32)treeTile.y))
    {
        tree = we_findAccesibleTree(context, forest, treeTile);
        if (!tree)
        {
            WarStateIdle* idleState = wst_createIdleState(context, entity, true);
            wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
            TracyCZoneEnd(ctx);
            return;
        }

        treeTile = vec2i(tree->tilex, tree->tiley);
        treePosition = wmap_tileToMapCoordinatesV(treeTile, true);
        s->position = treePosition;
    }

    if (!wu_tileInRange(context, entity, treeTile, stats->range))
    {
        vec2 position = wu_getUnitCenterPosition(context, entity);
        WarStateMove* moveState = wst_createMoveState(context, entity, 2, arrayArg(vec2, position, treePosition), false);
#ifdef WAR_FRANK
        if (moveState) moveState->frankRange = stats->range;   // next to the tree (the original's range 1)
#endif
        wst_pushState(context, entity, (WarStateBase*)moveState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    WarStateChopping* choppingState = wst_createChoppingState(context, entity, forest->id, treePosition);
    wst_replaceState(context, entity, (WarStateBase*)choppingState, WAR_TRANSITION_CAUSE_COMPLETION);

    TracyCZoneEnd(ctx);
}


void wst_updateWoodStates(WarContext* context)
{
    TracyCZoneN(ctx, "wst_updateWoodStates", true);

    WarEntityManager* manager = we_getEntityManager(context);
    WarStateStorage*  storage = &manager->stateStorage;
    WarStateWood*      states  = storage->wood;
    bool*             occupied = storage->occupied[WAR_STATE_WOOD];

    for (s32 i = 0; i < MAX_STATES_PER_TYPE; i++)
    {
        if (!occupied[i]) continue;

        WarStateWood*  state  = &states[i];
        WarEntity*    entity = we_findEntity(context, state->base.entityId);
        if (!entity) continue;

        if (wst_getActiveState(context, entity) != (WarStateBase*)state) continue;
        if (!wst_isNextUpdateTime(context, (WarStateBase*)state)) continue;

        wst_updateGatherWoodState(context, entity, (WarStateBase*)state);
    }

    TracyCZoneEnd(ctx);
}
