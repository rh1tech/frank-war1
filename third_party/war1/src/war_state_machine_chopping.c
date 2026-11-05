#include "war_state_machine.h"

#include "war_actions.h"
#include "war_audio.h"
#include "war_units.h"
#include "war_map.h"

#include "TracyC.h"

WarStateChopping* wst_createChoppingState(WarContext* context, WarEntity* entity, WarEntityId forestId, vec2 position)
{
    TracyCZoneN(ctx, "wst_createChoppingState", true);

    WarStateRef ref = wst_allocState(context, WAR_STATE_CHOPPING, entity->id);
    if (!WAR_STATE_REF_IS_VALID(ref))
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    WarStateChopping* state = (WarStateChopping*)wst_deref(context, ref);
    if (!state)
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    state->forestId = forestId;
    state->position = position;
#ifdef WAR_FRANK
    state->frankChops = 0;
#endif

    TracyCZoneEnd(ctx);
    return state;
}

void wst_enterChoppingState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_enterChoppingState", true);

    WarMap* map = context->map;
    assert(map);

    WarStateChopping* s = (WarStateChopping*)state;

    vec2 unitTile = wu_getUnitCenterTile(context, entity);
    vec2 unitSize = wu_getUnitSize(context, entity);
    vec2 treePosition = s->position;
    vec2 treeTile = wmap_mapToTileCoordinatesV(treePosition);

    wpath_setStaticEntity(&map->finder, (s32)unitTile.x, (s32)unitTile.y, (s32)unitSize.x, (s32)unitSize.y, entity->id);
    wu_setUnitDirectionFromDiff(context, entity, treeTile.x - unitTile.x, treeTile.y - unitTile.y);
    wact_setAction(context, entity, WAR_ACTION_TYPE_HARVEST, true, 1.0f);

    TracyCZoneEnd(ctx);
}

void wst_updateChoppingState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_updateChoppingState", true);
#ifdef WAR_FRANK
    if (wai_drivesTicks(context))
    {
        TracyCZoneEnd(ctx);
        return;
    }
#endif

    WarStateChopping* s = (WarStateChopping*)state;

    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    assert(unit);

    WarEntity* forest = we_findEntity(context, (WarEntityId)s->forestId);

    // if the forest doesn't exists, go idle
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

    if (!tree || tree->amount == 0)
    {
        WarStateWood* gatherWoodState = wst_createGatherWoodState(context, entity, forest->id, treePosition);
        wst_replaceState(context, entity, (WarStateBase*)gatherWoodState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    WarUnitAction* action = &unit->actions[unit->actionType];
#ifdef WAR_FRANK
    // frank: the AI's tick counts the strokes (wst_frankChopTick)
    if (wai_drivesTicks(context)) action->lastActionStep = WAR_ACTION_STEP_NONE;
#endif
    if (action->lastActionStep == WAR_ACTION_STEP_ATTACK)
    {
        unit->amount += we_chopTree(context, forest, tree, 2);
        if (unit->amount > 0)
        {
            unit->resourceKind = WAR_RESOURCE_WOOD;
        }

        if (action->lastSoundStep == WAR_ACTION_STEP_SOUND_CHOPPING)
        {
            wa_createAudioRandomWithPosition(context, CREATE_AUDIO_ARGS_INIT(
                .randomFromId=WAR_TREE_CHOPPING_1,
                .randomToId=WAR_TREE_CHOPPING_4,
                .position=treePosition,
                .hasPosition=true,
                .loop=false
            ));
        }

        if (unit->amount == UNIT_MAX_CARRY_WOOD)
        {
            // set the carrying gold sprites
            const WarWorkerData* workerData = wu_getWorkerData(unit->type);
            we_removeSpriteComponent(context, entity);
            we_addSpriteComponentFromResource(context, entity, imageResourceRef(workerData->carryingWoodResource));

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
            deliverState->sourceKind = WAR_RESOURCE_WOOD;
            deliverState->sourceId = forest->id;
            deliverState->sourcePosition = treePosition;
            wst_replaceState(context, entity, (WarStateBase*)deliverState, WAR_TRANSITION_CAUSE_COMPLETION);
        }

        // this is not the more elegant solution, but the actions and the state machine have to comunicate somehow
        action->lastActionStep = WAR_ACTION_STEP_NONE;
        action->lastSoundStep =  WAR_ACTION_STEP_NONE;
    }

    TracyCZoneEnd(ctx);
}


void wst_updateChoppingStates(WarContext* context)
{
    TracyCZoneN(ctx, "wst_updateChoppingStates", true);

    WarEntityManager* manager = we_getEntityManager(context);
    WarStateStorage*  storage = &manager->stateStorage;
    WarStateChopping*      states  = storage->chopping;
    bool*             occupied = storage->occupied[WAR_STATE_CHOPPING];

    for (s32 i = 0; i < MAX_STATES_PER_TYPE; i++)
    {
        if (!occupied[i]) continue;

        WarStateChopping*  state  = &states[i];
        WarEntity*    entity = we_findEntity(context, state->base.entityId);
        if (!entity) continue;

        if (wst_getActiveState(context, entity) != (WarStateBase*)state) continue;
        if (!wst_isNextUpdateTime(context, (WarStateBase*)state)) continue;

        wst_updateChoppingState(context, entity, (WarStateBase*)state);
    }

    TracyCZoneEnd(ctx);
}

#ifdef WAR_FRANK
// frank: the original's harvest order (0x1CD50) at each checkpoint of the
// worker's work script: 50 strokes counted, and at the next checkpoint the
// tree is down and the load (100) goes home.
void wst_frankChopTick(WarContext* context, WarEntity* entity, WarStateBase* st)
{
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    if (!unit) return;
    if (!st || st->type != WAR_STATE_CHOPPING)
    {
        if (!st || st->type != WAR_STATE_ATTACK) unit->frankAtkOn = false;
        return;
    }
    if (!unit->frankAtkOn)
    {
        if (!wos_select(context, entity, 10, 1)) return;
        unit->frankAtkOn = true;
    }
    if (wos_tick(context, entity, NULL) != WOR_CHANGED) return;
    if (wmv_takePendingOrder(context, entity)) return;
    wst_frankChopCheckpoint(context, entity, st);
}

void wst_frankChopCheckpoint(WarContext* context, WarEntity* entity, WarStateBase* st)
{
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    WarStateChopping* s = (WarStateChopping*)st;
    WarEntity* forest = we_findEntity(context, s->forestId);
    vec2 treeTile = wmap_mapToTileCoordinatesV(s->position);
    WarTree* tree = forest ? we_getTreeAtTile(context, forest, (s32)treeTile.x, (s32)treeTile.y) : NULL;
    if (!tree || !wmv_standingTree(context, (s32)treeTile.x, (s32)treeTile.y))
    {
        vec2 here = wu_getUnitCenterTile(context, entity);
        s32 x, y;
        if (wmv_treeAround(context, (s32)here.x, (s32)here.y, &x, &y))
            wst_replaceState(context, entity, (WarStateBase*)wst_createGatherWoodState(
                context, entity, s->forestId, wmap_tileToMapCoordinatesV(vec2i(x, y), true)), WAR_TRANSITION_CAUSE_COMPLETION);
        else
            wst_replaceState(context, entity, (WarStateBase*)wst_createIdleState(context, entity, false), WAR_TRANSITION_CAUSE_COMPLETION);
        wmv_settle(context, entity);
        return;
    }
    if (s->frankChops < 50) { s->frankChops++; return; }
    // done: the tree is down, the wood goes home
#ifndef FRANK_BOARD
    if (getenv("WAR_TURNDEBUG")) fprintf(stderr, "chopped by %d tree %d,%d amount %d\n", (s32)entity->id, (s32)treeTile.x, (s32)treeTile.y, tree ? tree->amount : -1);
#endif
    if (forest && tree) we_chopTree(context, forest, tree, TREE_MAX_WOOD);
    // (0x1CD50: carrying, and the tree kept to come back to)
    unit->frankWorkX = (s16)treeTile.x;
    unit->frankWorkY = (s16)treeTile.y;
    unit->frankReturning = true;
    unit->frankWorkSource = forest ? forest->id : 0;
    unit->frankWorkGold = false;
    unit->amount = UNIT_MAX_CARRY_WOOD;
    unit->resourceKind = WAR_RESOURCE_WOOD;
    const WarWorkerData* workerData = wu_getWorkerData(unit->type);
    we_removeSpriteComponent(context, entity);
    we_addSpriteComponentFromResource(context, entity, imageResourceRef(workerData->carryingWoodResource));
    WarEntity* townHall = wai_homeHall(context, entity);
#ifndef FRANK_BOARD
    if (getenv("WAR_TURNDEBUG")) fprintf(stderr, "deliver %d to %d\n", (s32)entity->id, townHall ? (s32)townHall->id : -1);
#endif
    if (!townHall)
    {
        wst_replaceState(context, entity, (WarStateBase*)wst_createIdleState(context, entity, true), WAR_TRANSITION_CAUSE_COMPLETION);
        return;
    }
    WarStateDeliver* deliverState = wst_createDeliverState(context, entity, townHall->id);
    deliverState->cycle = true;
    deliverState->sourceKind = WAR_RESOURCE_WOOD;
    deliverState->sourceId = s->forestId;
    deliverState->sourcePosition = s->position;
    wst_replaceState(context, entity, (WarStateBase*)deliverState, WAR_TRANSITION_CAUSE_COMPLETION);
    wmv_settle(context, entity);
}
#endif
