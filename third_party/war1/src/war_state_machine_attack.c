#include "war_state_machine.h"

#include "war.h"
#include "war_map.h"
#include "war_actions.h"
#include "war_audio.h"
#include "war_units.h"

#include "TracyC.h"

#ifdef WAR_FRANK
#include "../../../port/war_fight_frank.inc"
#endif

WarStateAttack* wst_createAttackState(WarContext* context, WarEntity* entity, WarEntityId targetEntityId, vec2 targetPosition)
{
    TracyCZoneN(ctx, "wst_createAttackState", true);

    WarStateRef ref = wst_allocState(context, WAR_STATE_ATTACK, entity->id);
    if (!WAR_STATE_REF_IS_VALID(ref))
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    WarStateAttack* state = (WarStateAttack*)wst_deref(context, ref);
    if (!state)
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    state->targetEntityId = targetEntityId;
    state->targetPosition = targetPosition;
#ifdef WAR_FRANK
    state->frankOrder = 5;
    state->frankPursue = true;
    state->frankPrepared = false;
#endif

    TracyCZoneEnd(ctx);
    return state;
}

#ifdef WAR_FRANK
void wst_enterAttackState(WarContext* context, WarEntity* entity, WarStateBase* state)
{
    NOT_USED(state);
    if (!wai_drivesTicks(context)) return;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    if (!unit) return;
    unit->frankAtkOn = false;
    unit->frankAtkCheck = false;
    unit->frankScriptChanged = true;
}

void wst_resumeAttackState(WarContext* context, WarEntity* entity, WarStateBase* state, WarStateResumeReason reason)
{
    NOT_USED(reason);
    wst_enterAttackState(context, entity, state);
}

// Resolve the command's move/action pair once; arriving later selects the
// action script without executing its handler at the same checkpoint (0x22554, 0x2EE9C).
static void wst_frankPrepareAttack(WarContext* context, WarEntity* entity, WarStateAttack* state)
{
    state->frankPrepared = true;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    unit->wmvPathIndex = 20;
    unit->wmvBlocks = 0;
    if (!state->frankPursue) return;
    WarEntity* target = state->targetEntityId ? we_findEntity(context, state->targetEntityId) : NULL;
    if (state->targetEntityId && !target) return;
    if (!target)
    {
        vec2 tile = wmap_mapToTileCoordinatesV(state->targetPosition);
        tile = wmv_backOff(context, entity, (s32)tile.x, (s32)tile.y);
        state->targetPosition = wmap_tileToMapCoordinatesV(tile, true);
        WarOriginalUnit* original = wos_unit(context, entity);
        if (original)
        {
            original->goalX = (s16)tile.x;
            original->goalY = (s16)tile.y;
        }
    }
    const vec2 goal = target ? wu_getUnitCenterPosition(context, target) : state->targetPosition;
    const vec2 tile = wmap_mapToTileCoordinatesV(goal);
    const s32 range = state->frankOrder == 7 ? 1
        : target || wmv_hasTerrain((s32)tile.x, (s32)tile.y, 0xC) ? wu_getUnitStats(unit->type)->range : 0;
    const bool arrived = target && wu_isUnit(target) ? wu_unitInRange(context, entity, target, range)
        : wu_tileInRange(context, entity, wmap_mapToTileCoordinatesV(goal), range);
    if (arrived) return;
    WarStateMove* move = wst_createMoveState(context, entity, 2,
        arrayArg(vec2, wu_getUnitCenterPosition(context, entity), goal), false);
    if (!move) return;
    move->frankTarget = target && wu_isUnit(target) ? target->id : 0;
    move->frankRange = range;
    move->frankNextOrder = state->frankOrder;
    wst_pushState(context, entity, (WarStateBase*)move, WAR_TRANSITION_CAUSE_COMPLETION);
}
#endif

void wst_updateAttackState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_updateAttackState", true);

    WarStateAttack* s = (WarStateAttack*)state;

#ifdef WAR_FRANK
    if (wai_drivesTicks(context))
    {
        if (!s->frankPrepared) wst_frankPrepareAttack(context, entity, s);
        TracyCZoneEnd(ctx);
        return;
    }
#endif
    WarMap* map = context->map;
    assert(map);

    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    assert(unit);

    vec2 position = wu_getUnitCenterPosition(context, entity);
    vec2 unitTile = wu_getUnitCenterTile(context, entity);
    vec2 unitSize = wu_getUnitSize(context, entity);

    const WarUnitStats* stats = wu_getUnitStats(unit->type);

    WarEntityId targetEntityId = (WarEntityId)s->targetEntityId;
    WarEntity* targetEntity = we_findEntity(context, targetEntityId);

    vec2 targetPosition = s->targetPosition;
    vec2 targetTile = wmap_mapToTileCoordinatesV(targetPosition);

    // if the entity to attack doesn't exists, go to the attacking point or go idle
#ifdef WAR_FRANK
    // frank: a unit's target gone: the end comes at the attack script's
    // checkpoint (port/war_fight_frank.inc)
    if (!targetEntity && targetEntityId && wai_drivesTicks(context))
    {
        TracyCZoneEnd(ctx);
        return;
    }
#endif
    if (!targetEntity)
    {
        // when going to an attacking point (where there is no target unit)
        // check if the attacking unit is in range 1, no matter if the range
        // of the attacking unit is greater
        if(!wu_tileInRange(context, entity, targetTile, 1))
        {
            WarStateMove* moveState = wst_createMoveState(context, entity, 2, arrayArg(vec2, position, targetPosition), true);
            wst_pushState(context, entity, (WarStateBase*)moveState, WAR_TRANSITION_CAUSE_COMPLETION);
            TracyCZoneEnd(ctx);
            return;
        }

        wst_popState(context, entity, WAR_TRANSITION_CAUSE_COMPLETION, WAR_STATE_RESULT_SUCCESS);
        TracyCZoneEnd(ctx);
        return;
    }

    if (wu_isUnit(targetEntity))
    {
        // if the target entity is an unit the instead of using the tile where
        // the player click, use a point on the target unit that is closer to
        // the attacking unit
        targetPosition = wu_unitPointOnTarget(context, entity, targetEntity);
        targetTile = wmap_mapToTileCoordinatesV(targetPosition);
    }

    if (wu_isUnit(targetEntity) && !wu_unitInRange(context, entity, targetEntity, stats->range))
    {
        WarStateFollow* followState = wst_createFollowState(context, entity, targetEntityId, targetPosition, stats->range * MEGA_TILE_WIDTH);
        wst_pushState(context, entity, (WarStateBase*)followState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    if(wu_isWall(targetEntity) && !wu_tileInRange(context, entity, targetTile, stats->range))
    {
        WarStateFollow* followState = wst_createFollowState(context, entity, 0, targetPosition, stats->range * MEGA_TILE_WIDTH);
        wst_pushState(context, entity, (WarStateBase*)followState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    // if the unit is attacking a worker that is currently gathering and inside of the goldmine or the townhall,
    // wcmd_stop the attacking for a moment until the unit come out again
    if (wst_isInsideBuilding(context, targetEntity))
    {
        WarStateWait* waitState = wst_createWaitState(context, entity, 1.0f);
        wst_pushState(context, entity, (WarStateBase*)waitState, WAR_TRANSITION_CAUSE_COMPLETION);
        TracyCZoneEnd(ctx);
        return;
    }

    wpath_setStaticEntity(&map->finder, (s32)unitTile.x, (s32)unitTile.y, (s32)unitSize.x, (s32)unitSize.y, entity->id);
    wu_setUnitDirectionFromDiff(context, entity, targetTile.x - unitTile.x, targetTile.y - unitTile.y);
    wact_setAction(context, entity, WAR_ACTION_TYPE_ATTACK, false, 1.0f);

    WarUnitAction* action = &unit->actions[unit->actionType];
#if defined(WAR_FRANK) && !defined(FRANK_BOARD)
    if (getenv("WAR_MOVEDEBUG") && atoi(getenv("WAR_MOVEDEBUG")) == (s32)entity->id)
        fprintf(stderr, "attack %.3f act %d step %d last %d\n", context->gameTime, unit->actionType, action->stepIndex, action->lastActionStep);
#endif
    if (action->lastActionStep == WAR_ACTION_STEP_ATTACK)
    {
        // when the unit begin an attack, it is not invisible anymore
        unit->invisible = false;
        unit->invisibilityTime = 0;

        // do damage
#ifdef WAR_FRANK
        // frank: the blow is the original's (port/war_fight_frank.inc), on
        // its tick; here only the end of a dead target
        if (wai_drivesTicks(context) && wu_isUnit(targetEntity))
        {
            // (the end of a dead target too: at the attack script's
            // checkpoint, port/war_fight_frank.inc)
        }
        else
#endif
        if (wu_isUnit(targetEntity))
        {
            // if the target entity is dead or is collapsing (in case of buildings), go to idle
            // do this check before apply damage in case of multiple units attacking.
            // one of them could cause the unit to die, so the other should wcmd_stop doing further damage.
            if (wst_isDead(context, targetEntity) || wst_isGoingToDie(context, targetEntity) ||
                wst_isCollapsing(context, targetEntity) || wst_isGoingToCollapse(context, targetEntity))
            {
                wst_popState(context, entity, WAR_TRANSITION_CAUSE_COMPLETION, WAR_STATE_RESULT_SUCCESS);
            }
            else
            {
                if (wu_isRangeUnit(context, entity))
                {
                    we_rangeAttack(context, entity, targetEntity);
                }
                else
                {
                    we_meleeAttack(context, entity, targetEntity);
                }

                if (context->gameTime - unit->lastAttackSoundGameTime >= MIN_ATTACK_SOUND_INTERVAL)
                {
                    vec2 targetEntityPosition = wu_getUnitCenterPosition(context, targetEntity);
                    wa_playAttackSound(context, targetEntityPosition, action->lastSoundStep);
                    unit->lastAttackSoundGameTime = context->gameTime;
                }
            }
        }
        else if(wu_isWall(targetEntity))
        {
            WarWallPiece* piece = we_getWallPieceAtPosition(context, targetEntity, (s32)targetTile.x, (s32)targetTile.y);
            if (piece)
            {
                // if the piece of the wall the unit is attacking has no more hit points, go to idle.
                // do this check before apply damage in case of multiple units attacking.
                // one of them could destroy the piece, so the other should wcmd_stop doing further damage.
                if (piece->hp == 0)
                {
                    wst_popState(context, entity, WAR_TRANSITION_CAUSE_COMPLETION, WAR_STATE_RESULT_SUCCESS);
                }
                else
                {
                    if (wu_isRangeUnit(context, entity))
                    {
                        we_rangeWallAttack(context, entity, targetEntity, piece);
                    }
                    else
                    {
                        we_meleeWallAttack(context, entity, targetEntity, piece);
                    }

                    if (context->gameTime - unit->lastAttackSoundGameTime >= MIN_ATTACK_SOUND_INTERVAL)
                    {
                        vec2 targetEntityPosition = wmap_tileToMapCoordinatesV(targetTile, true);
                        wa_playAttackSound(context, targetEntityPosition, action->lastSoundStep);
                        unit->lastAttackSoundGameTime = context->gameTime;
                    }
                }
            }
        }

        // this is not the more elegant solution, but the actions and the state machine have to comunicate somehow
        action->lastActionStep = WAR_ACTION_STEP_NONE;
        action->lastSoundStep =  WAR_ACTION_STEP_NONE;
    }

    TracyCZoneEnd(ctx);
}


void wst_updateAttackStates(WarContext* context)
{
    TracyCZoneN(ctx, "wst_updateAttackStates", true);

    WarEntityManager* manager = we_getEntityManager(context);
    WarStateStorage*  storage = &manager->stateStorage;
    WarStateAttack*      states  = storage->attack;
    bool*             occupied = storage->occupied[WAR_STATE_ATTACK];

    for (s32 i = 0; i < MAX_STATES_PER_TYPE; i++)
    {
        if (!occupied[i]) continue;

        WarStateAttack*  state  = &states[i];
        WarEntity*    entity = we_findEntity(context, state->base.entityId);
        if (!entity) continue;

        if (wst_getActiveState(context, entity) != (WarStateBase*)state) continue;
#ifdef WAR_FRANK
        if (wai_drivesTicks(context))
        {
            WarEntity* target = state->targetEntityId ? we_findEntity(context, state->targetEntityId) : NULL;
            if (!target || wu_isUnit(target)) continue; // unit combat belongs to the script checkpoint (0x2ECFC)
        }
#endif
        if (!wst_isNextUpdateTime(context, (WarStateBase*)state)) continue;

        wst_updateAttackState(context, entity, (WarStateBase*)state);
    }

    TracyCZoneEnd(ctx);
}
