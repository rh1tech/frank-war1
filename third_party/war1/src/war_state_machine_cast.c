#include "war_state_machine.h"

#include "war_actions.h"
#include "war_animations.h"
#include "war_audio.h"
#include "war_projectiles.h"
#include "war_units.h"
#include "war_map.h"

#include "TracyC.h"

WarStateCast* wst_createCastState(WarContext* context, WarEntity* entity, WarSpellType spellType, WarEntityId targetEntityId, vec2 targetPosition)
{
    TracyCZoneN(ctx, "wst_createCastState", true);

    WarStateRef ref = wst_allocState(context, WAR_STATE_CAST, entity->id);
    if (!WAR_STATE_REF_IS_VALID(ref))
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    WarStateCast* state = (WarStateCast*)wst_deref(context, ref);
    if (!state)
    {
        TracyCZoneEnd(ctx);
        return NULL;
    }

    state->spellType = spellType;
    state->targetEntityId = targetEntityId;
    state->targetPosition = targetPosition;
#ifdef WAR_FRANK
    state->frankPrepared = false;
    state->frankRunning = false;
    state->frankNextOrder = 2;
#endif

    TracyCZoneEnd(ctx);
    return state;
}

bool wst_canInterruptCast(WarContext* context, WarEntity* entity, WarStateBase* state, WarInterruptKind interrupt)
{
    NOT_USED(context);
    NOT_USED(entity);

    WarStateCast* s = (WarStateCast*)state;

    switch (s->spellType)
    {
        case WAR_SPELL_RAIN_OF_FIRE:
            return interrupt & (WAR_INTERRUPT_PLAYER_ORDER | WAR_INTERRUPT_AI_ORDER | WAR_INTERRUPT_STATUS | WAR_INTERRUPT_SCRIPT | WAR_INTERRUPT_LIFECYCLE);
        case WAR_SPELL_HEALING:
        case WAR_SPELL_FAR_SIGHT:
        case WAR_SPELL_INVISIBILITY:
        case WAR_SPELL_POISON_CLOUD:
        case WAR_SPELL_RAISE_DEAD:
        case WAR_SPELL_DARK_VISION:
        case WAR_SPELL_UNHOLY_ARMOR:
        case WAR_SUMMON_SPIDER:
        case WAR_SUMMON_SCORPION:
        case WAR_SUMMON_DAEMON:
        case WAR_SUMMON_WATER_ELEMENTAL:
            return true;
    }

    return false;
}

#ifdef WAR_FRANK
// Spells keep command entry, script action and checkpoint completion
// separate (0x21258, 0x14140, 0x27BCC).
u8 wst_frankSpellOrder(WarSpellType type)
{
    switch (type)
    {
        case WAR_SPELL_HEALING: return 17;
        case WAR_SPELL_INVISIBILITY: return 19;
        case WAR_SPELL_RAISE_DEAD: return 20;
        case WAR_SPELL_UNHOLY_ARMOR: return 21;
        case WAR_SUMMON_WATER_ELEMENTAL: return 22;
        case WAR_SUMMON_DAEMON: return 23;
        case WAR_SUMMON_SPIDER: return 24;
        case WAR_SUMMON_SCORPION: return 25;
        case WAR_SPELL_RAIN_OF_FIRE: return 26;
        case WAR_SPELL_POISON_CLOUD: return 27;
        default: return 0;
    }
}

static void wst_frankCastIdle(WarContext* context, WarEntity* entity)
{
    WarStateIdle* idle = wst_createIdleState(context, entity, false);
    if (!idle || !wst_resetState(context, entity, (WarStateBase*)idle, WAR_TRANSITION_CAUSE_COMPLETION))
    {
        logError("original kernel: failed spell completion for entity %u", entity->id);
        return;
    }
    wmv_settle(context, entity);
}

static void wst_frankPrepareCast(WarContext* context, WarEntity* entity, WarStateCast* state)
{
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return;
    const u8 order = wst_frankSpellOrder(state->spellType);
    if (!order)
    {
        logError("original kernel: unsupported spell %d for entity %u", state->spellType, entity->id);
        return;
    }
    const bool targeted = order == 17 || order == 19 || order == 21;
    const bool pointed = order == 20 || order == 26 || order == 27;
    if (!state->frankPrepared && (targeted || pointed))
    {
        WarEntity* target = targeted ? we_findEntity(context, state->targetEntityId) : NULL;
        WarUnitComponent* other = target && wu_isUnit(target) ? we_getUnitComponent(context, target) : NULL;
        if (targeted && (!other || (order != 17 && wu_isBuildingUnitType(other->type))))
        {
            wst_frankCastIdle(context, entity);
            return;
        }
        WarUnitComponent* unit = we_getUnitComponent(context, entity);
        unit->wmvPathIndex = 20;
        unit->wmvBlocks = 0;
        original->target = other ? other->frankOriginalSlot : 0;
        vec2 tile = target ? wu_getUnitCenterTile(context, target) : wmap_mapToTileCoordinatesV(state->targetPosition);
        if (!target)
        {
            // every command's targetless goal backs off a blocked tile
            // along its line (0x2E1F0 through 0x22554 and 0x224AC)
            tile = wmv_backOff(context, entity, (s32)tile.x, (s32)tile.y);
            state->targetPosition = wmap_tileToMapCoordinatesV(tile, true);
        }
        original->goalX = (s16)tile.x;
        original->goalY = (s16)tile.y;
        if (target) state->targetPosition = wu_getUnitCenterPosition(context, target);
        state->frankPrepared = true;
        const u8 range = targeted ? 8 : order == 20 ? 5 : 9;
        if (!(target ? wu_unitInRange(context, entity, target, range) : wu_tileInRange(context, entity, tile, range)))
        {
            WarStateMove* move = wst_createMoveState(context, entity, 2,
                arrayArg(vec2, wu_getUnitCenterPosition(context, entity), state->targetPosition), false);
            if (!move)
            {
                logError("original kernel: failed spell movement for entity %u", entity->id);
                wst_frankCastIdle(context, entity);
                return;
            }
            move->frankTarget = target ? target->id : 0;
            move->frankRange = range;
            move->frankNextOrder = order;
            if (!wst_pushState(context, entity, (WarStateBase*)move, WAR_TRANSITION_CAUSE_COMPLETION))
            {
                logError("original kernel: rejected spell movement for entity %u", entity->id);
                wst_frankCastIdle(context, entity);
            }
            return;
        }
    }
    if (!state->frankRunning)
    {
        if (!wos_startPair(context, entity, order, targeted || pointed ? WOR_NONE : state->frankNextOrder)) return;
        state->frankPrepared = true;
        state->frankRunning = true;
        we_getUnitComponent(context, entity)->frankScriptChanged = true;
    }
}

static void wst_frankCastAction(WarContext* context, WarEntity* entity)
{
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return;
    if (original->current >= 22 && original->current <= 25)
        wos_summon(context, entity, true);
    else if (original->current == 20)
        wos_resurrect(context, entity, true);
    else if (original->current == 26)
        wft_rain(context, entity, true);
    else if (original->current == 27)
        wft_poison(context, entity, true);
    else if (wos_targetSpell(context, entity, true) == WOR_CHANGED)
    {
        WarEntity* target = we_getEntityManager(context)->originalEntities[original->target - 1];
        wft_poolVisual(context, 8, target);   // (0x27760, 0x27890, 0x278C4 through 0x11EC0)
        if (target)
        {
            vec2 position = wu_getUnitCenterPosition(context, target);
            WarEntity* effect = we_createEntity(context, WAR_ENTITY_TYPE_ANIMATION, true);
            if (effect)
            {
                we_addAnimationsComponent(context, effect);
                wanim_createSpellAnimation(context, effect, position);
                wa_createAudioWithPosition(context, CREATE_AUDIO_ARGS_INIT(
                    .audioId=WAR_NORMAL_SPELL, .position=position, .hasPosition=true, .loop=false));
            }
        }
    }
    // rain goes on until it cannot pay (0x27BCC, 0x2793C)
    if (original->current != 26) wos_queue(context, entity, 2, WOR_NONE);
}

void wst_frankCastTick(WarContext* context, WarEntity* entity, WarStateBase* state)
{
    WarStateCast* cast = (WarStateCast*)state;
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    if (unit->frankScriptChanged) return;
    if (!cast->frankRunning)
    {
        wst_frankPrepareCast(context, entity, cast);
        wmv_settle(context, entity);
        return;
    }
    if (wos_tick(context, entity, wst_frankCastAction) != WOR_CHANGED) return;
    if (wmv_takePendingOrder(context, entity)) return;
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return;
    if (original->pending == 2)
    {
        wst_frankCastIdle(context, entity);
        return;
    }
    if (original->current >= 22 && original->current <= 25)
    {
        if (wos_summon(context, entity, false) != WOR_CHANGED)
            wst_frankCastIdle(context, entity);
        else
            wact_setAction(context, entity, WAR_ACTION_TYPE_ATTACK, false, 1.0f);
        return;
    }
    if (original->current == 20 || original->current == 26 || original->current == 27)
    {
        const vec2 here = wu_getUnitCenterTile(context, entity);
        wu_setUnitDirection(context, entity, (WarDirection)wor_facing(
            (s16)(original->goalX - here.x), (s16)(original->goalY - here.y)));
        if ((original->current == 20 ? wos_resurrect(context, entity, false)
             : original->current == 26 ? wft_rain(context, entity, false)
                                       : wft_poison(context, entity, false)) != WOR_CHANGED)
            wst_frankCastIdle(context, entity);
        else
            wact_setAction(context, entity, WAR_ACTION_TYPE_ATTACK, false, 1.0f);
        return;
    }
    WarEntityManager* manager = we_getEntityManager(context);
    WarOriginalUnit* recipient = original->target ? &manager->originalUnits[original->target - 1] : NULL;
    WarEntity* target = original->target ? manager->originalEntities[original->target - 1] : NULL;
    if (!recipient || !target || (recipient->flags & (WOR_INACTIVE | WOR_INSIDE)) || recipient->invisibility)
    {
        wst_frankCastIdle(context, entity);
        return;
    }
    if (!wu_unitInRange(context, entity, target, 8))
    {
        cast->frankPrepared = false;
        cast->frankRunning = false;
        wst_frankPrepareCast(context, entity, cast);
        wmv_settle(context, entity);
        return;
    }
    const vec2 here = wu_getUnitCenterTile(context, entity), there = wu_getUnitCenterTile(context, target);
    wu_setUnitDirection(context, entity, (WarDirection)wor_facing(
        (s16)(there.x - here.x), (s16)(there.y - here.y)));
    if (wos_targetSpell(context, entity, false) != WOR_CHANGED)
    {
        wst_frankCastIdle(context, entity);
        return;
    }
    wact_setAction(context, entity, WAR_ACTION_TYPE_ATTACK, false, 1.0f);
}
#endif

void wst_updateCastState(WarContext* context, WarEntity* entity, WarState* state)
{
    TracyCZoneN(ctx, "wst_updateCastState", true);

    WarStateCast* s = (WarStateCast*)state;
#ifdef WAR_FRANK
    if (wai_drivesTicks(context) && wst_frankSpellOrder(s->spellType))
    {
        wst_frankPrepareCast(context, entity, s);
        TracyCZoneEnd(ctx);
        return;
    }
#endif

    WarMap* map = context->map;

    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    assert(unit);

    vec2 unitTile = wu_getUnitCenterTile(context, entity);
    vec2 unitSize = wu_getUnitSize(context, entity);

    WarSpellType spellType = s->spellType;
    WarEntityId targetEntityId = s->targetEntityId;
    vec2 targetPosition = s->targetPosition;
    vec2 targetTile = wmap_mapToTileCoordinatesV(targetPosition);

    const WarSpellStats* stats = wu_getSpellStats(spellType);

    if (stats->range)
    {
        if(!wu_tileInRange(context, entity, targetTile, stats->range))
        {
            WarStateFollow* followState = wst_createFollowState(context, entity, targetEntityId, targetPosition, stats->range * MEGA_TILE_WIDTH);
            wst_pushState(context, entity, (WarStateBase*)followState, WAR_TRANSITION_CAUSE_COMPLETION);
            TracyCZoneEnd(ctx);
            return;
        }
    }

    wpath_setStaticEntity(&map->finder, (s32)unitTile.x, (s32)unitTile.y, (s32)unitSize.x, (s32)unitSize.y, entity->id);
    wu_setUnitDirectionFromDiff(context, entity, targetTile.x - unitTile.x, targetTile.y - unitTile.y);
    wact_setAction(context, entity, WAR_ACTION_TYPE_ATTACK, false, 1.0f);

    WarUnitAction* action = &unit->actions[unit->actionType];
    if (action->lastActionStep == WAR_ACTION_STEP_ATTACK)
    {
        // when the unit cast a spell, it is not invisible anymore
        unit->invisible = false;
        unit->invisibilityTime = 0;

        switch (spellType)
        {
            case WAR_SPELL_HEALING:
            {
                WarEntity* targetEntity = we_findEntity(context, targetEntityId);
                if (targetEntity && wu_isDudeUnit(context, targetEntity))
                {
                    WarUnitComponent* targetUnit = we_getUnitComponent(context, targetEntity);
                    assert(targetUnit);

                    // the healing spell's strength is determined by units of mana.
                    // for every 6 units of mana, the damaged unit gets back 1 hit point.
                    //
                    // take all the hp the cleric can restore according to its mana
                    s32 hpToRestore = unit->mana / stats->manaCost;

                    // take in reality how much hp needs to be restored
                    hpToRestore = MIN(hpToRestore, targetUnit->maxhp - targetUnit->hp);

                    // recalculate how much mana the cleric need to spend
                    s32 manaToSpend = hpToRestore * stats->manaCost;

                    we_increaseUnitHp(context, targetEntity, hpToRestore);
                    we_decreaseUnitMana(context, entity, manaToSpend);

                    vec2 targetEntityPosition = wu_getUnitCenterPosition(context, targetEntity);

                    WarEntity* animEntity = we_createEntity(context, WAR_ENTITY_TYPE_ANIMATION, true);
                    we_addAnimationsComponent(context, animEntity);

                    wanim_createSpellAnimation(context, animEntity, targetEntityPosition);
                    wa_createAudioWithPosition(context, CREATE_AUDIO_ARGS_INIT(.audioId=WAR_NORMAL_SPELL, .position=targetEntityPosition, .hasPosition=true, .loop=false));
                }

                WarStateIdle* idleState = wst_createIdleState(context, entity, true);
                wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);

                break;
            }

            case WAR_SPELL_FAR_SIGHT:
            case WAR_SPELL_DARK_VISION:
            {
                if (we_decreaseUnitMana(context, entity, stats->manaCost))
                {
                    vec2 targetEntityPosition = wmap_tileToMapCoordinatesV(targetTile, true);

                    WarEntity* sight = we_createEntity(context, WAR_ENTITY_TYPE_SIGHT, true);
                    we_addSightComponent(context, sight, WAR_SIGHT_COMPONENT_INIT(
                        .position = targetTile,
                        .time     = stats->time,
                    ));
                    we_addAnimationsComponent(context, sight);

                    wanim_createSpellAnimation(context, sight, targetEntityPosition);
                    wa_createAudioWithPosition(context, CREATE_AUDIO_ARGS_INIT(.audioId=WAR_NORMAL_SPELL, .position=targetEntityPosition, .hasPosition=true, .loop=false));
                }

                WarStateIdle* idleState = wst_createIdleState(context, entity, true);
                wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);

                break;
            }

            case WAR_SPELL_INVISIBILITY:
            {
                WarEntity* targetEntity = we_findEntity(context, targetEntityId);
                if (targetEntity && wu_isDudeUnit(context, targetEntity))
                {
                    WarUnitComponent* targetUnit = we_getUnitComponent(context, targetEntity);
                    assert(targetUnit);

                    if (we_decreaseUnitMana(context, entity, stats->manaCost))
                    {
                        targetUnit->invisible = true;
                        targetUnit->invisibilityTime = stats->time;

                        vec2 targetEntityPosition = wu_getUnitCenterPosition(context, targetEntity);

                        WarEntity* animEntity = we_createEntity(context, WAR_ENTITY_TYPE_ANIMATION, true);
                        we_addAnimationsComponent(context, animEntity);

                        wanim_createSpellAnimation(context, animEntity, targetEntityPosition);
                        wa_createAudioWithPosition(context, CREATE_AUDIO_ARGS_INIT(.audioId=WAR_NORMAL_SPELL, .position=targetEntityPosition, .hasPosition=true, .loop=false));
                    }
                }

                WarStateIdle* idleState = wst_createIdleState(context, entity, true);
                wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);

                break;
            }

            case WAR_SPELL_RAIN_OF_FIRE:
            {
                if (we_decreaseUnitMana(context, entity, stats->manaCost))
                {
                    vec2 targetEntityPosition = wmap_tileToMapCoordinatesV(targetTile, true);
                    s32 radius = 2 * MEGA_TILE_WIDTH;

                    s32 projectilesCount = 5;
                    while (projectilesCount--)
                    {
                        f32 offsetx = randomf(-radius, radius);
                        f32 offsety = randomf(-radius, radius);
                        vec2 target = vec2_addv(targetEntityPosition, vec2f(offsetx, offsety));

                        offsety = randomf(MEGA_TILE_WIDTH, MEGA_TILE_WIDTH * 4);
                        vec2 origin = vec2f(target.x, map->camera.viewport.y - offsety);

                        wproj_createProjectile(context, WAR_PROJECTILE_RAIN_OF_FIRE, 0, 0, origin, target);
                    }
                }
                else
                {
                    WarStateIdle* idleState = wst_createIdleState(context, entity, true);
                    wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
                }

                break;
            }

            case WAR_SPELL_RAISE_DEAD:
            {
                WarEntityList* nearUnits = (WarEntityList*)wm_allocFrame(sizeof(WarEntityList));
                WarEntityListInit(nearUnits, wm_frameAllocator());
                we_getNearUnits(context, targetTile, 4, nearUnits);

                for (s32 i = 0; i < nearUnits->count; i++)
                {
                    WarEntity* targetEntity = nearUnits->items[i];
                    if (targetEntity && wu_isCorpseUnit(context, targetEntity))
                    {
                        if (we_decreaseUnitMana(context, entity, stats->manaCost))
                        {
                            vec2 targetEntityTile = wu_getUnitCenterTile(context, targetEntity);
                            vec2 targetEntityPosition = wu_getUnitCenterPosition(context, targetEntity);

                            WarEntity* skeleton = we_createUnit(context, CREATE_UNIT_ARGS_INIT(
                                .type=WAR_UNIT_SKELETON,
                                .x=(s32)targetEntityTile.x,
                                .y=(s32)targetEntityTile.y,
                                .player=unit->player,
                                .resourceKind=WAR_RESOURCE_NONE,
                                .amount=0,
                                .addToMap=true
                            ));
                            we_setInitialIdleState(context, skeleton);

                            WarEntity* animEntity = we_createEntity(context, WAR_ENTITY_TYPE_ANIMATION, true);
                            we_addAnimationsComponent(context, animEntity);

                            wanim_createSpellAnimation(context, animEntity, targetEntityPosition);
                            wa_createAudioWithPosition(context, CREATE_AUDIO_ARGS_INIT(.audioId=WAR_NORMAL_SPELL, .position=targetEntityPosition, .hasPosition=true, .loop=false));

                            we_removeEntityById(context, targetEntity->id);
                        }
                    }
                }

                WarEntityListFree(nearUnits);

                WarStateIdle* idleState = wst_createIdleState(context, entity, true);
                wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);
                break;
            }

            case WAR_SPELL_UNHOLY_ARMOR:
            {
                WarEntity* targetEntity = we_findEntity(context, targetEntityId);
                if (targetEntity && wu_isDudeUnit(context, targetEntity))
                {
                    WarUnitComponent* targetUnit = we_getUnitComponent(context, targetEntity);
                    assert(targetUnit);

                    if (we_decreaseUnitMana(context, entity, stats->manaCost))
                    {
                        we_decreaseUnitHp(context, targetEntity, targetUnit->hp/2);

                        targetUnit->invulnerable = true;
                        targetUnit->invulnerabilityTime = stats->time;

                        vec2 targetEntityPosition = wu_getUnitCenterPosition(context, targetEntity);

                        WarEntity* animEntity = we_createEntity(context, WAR_ENTITY_TYPE_ANIMATION, true);
                        we_addAnimationsComponent(context, animEntity);

                        wanim_createSpellAnimation(context, animEntity, targetEntityPosition);
                        wa_createAudioWithPosition(context, CREATE_AUDIO_ARGS_INIT(.audioId=WAR_NORMAL_SPELL, .position=targetEntityPosition, .hasPosition=true, .loop=false));
                    }
                }

                WarStateIdle* idleState = wst_createIdleState(context, entity, true);
                wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);

                break;
            }

            case WAR_SPELL_POISON_CLOUD:
            {
                if (we_decreaseUnitMana(context, entity, stats->manaCost))
                {
                    vec2 targetEntityPosition = wmap_tileToMapCoordinatesV(targetTile, true);

                    WarEntity* poisonCloud = we_createEntity(context, WAR_ENTITY_TYPE_POISON_CLOUD, true);
                    we_addPoisonCloudComponent(context, poisonCloud, WAR_POISON_CLOUD_COMPONENT_INIT(
                        .tile = targetTile,
                        .time     = stats->time,
                    ));
                    we_addAnimationsComponent(context, poisonCloud);

                    wanim_createPoisonCloudAnimation(context, poisonCloud, targetEntityPosition);
                    wa_createAudioWithPosition(context, CREATE_AUDIO_ARGS_INIT(.audioId=WAR_NORMAL_SPELL, .position=targetEntityPosition, .hasPosition=true, .loop=false));
                }

                WarStateIdle* idleState = wst_createIdleState(context, entity, true);
                wst_replaceState(context, entity, (WarStateBase*)idleState, WAR_TRANSITION_CAUSE_COMPLETION);

                break;
            }

            default:
            {
                logWarning("Trying to cast wrong spell: %d", spellType);
                break;
            }
        }

        // this is not the more elegant solution, but the actions and the state machine have to comunicate somehow
        action->lastActionStep = WAR_ACTION_STEP_NONE;
        action->lastSoundStep =  WAR_ACTION_STEP_NONE;
    }

    TracyCZoneEnd(ctx);
}


void wst_updateCastStates(WarContext* context)
{
    TracyCZoneN(ctx, "wst_updateCastStates", true);

    WarEntityManager* manager = we_getEntityManager(context);
    WarStateStorage*  storage = &manager->stateStorage;
    WarStateCast*      states  = storage->cast;
    bool*             occupied = storage->occupied[WAR_STATE_CAST];

    for (s32 i = 0; i < MAX_STATES_PER_TYPE; i++)
    {
        if (!occupied[i]) continue;

        WarStateCast*  state  = &states[i];
        WarEntity*    entity = we_findEntity(context, state->base.entityId);
        if (!entity) continue;

        if (wst_getActiveState(context, entity) != (WarStateBase*)state) continue;
        if (!wst_isNextUpdateTime(context, (WarStateBase*)state)) continue;

        wst_updateCastState(context, entity, (WarStateBase*)state);
    }

    TracyCZoneEnd(ctx);
}
