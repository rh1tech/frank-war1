// Game-facing script adapter; order bodies remain in their existing handlers.
// SPDX-License-Identifier: Zlib
#pragma once

#include "war_orders_frank.h"

WarOriginalUnit* wos_unit(WarContext* context, WarEntity* entity);
bool wos_select(WarContext* context, WarEntity* entity, u8 order, u8 wait);
bool wos_startPair(WarContext* context, WarEntity* entity, u8 pending, u8 next);
bool wos_queue(WarContext* context, WarEntity* entity, u8 pending, u8 next);
WarOrderResult wos_targetSpell(WarContext* context, WarEntity* entity, bool action);
WarOrderResult wos_summon(WarContext* context, WarEntity* entity, bool action);
WarOrderResult wos_resurrect(WarContext* context, WarEntity* entity, bool action);
WarOrderResult wos_shotMana(WarContext* context, WarEntity* entity, bool action);
WarOrderResult wos_checkCatapult(WarContext* context, WarEntity* entity, WarEntity* target);
void wos_tickStatuses(WarContext* context);
WarOrderResult wos_takeNext(WarContext* context, WarEntity* entity);
bool wos_atWorkCheckpoint(const WarEntity* entity);
void wos_workHandler(WarContext* context, WarEntity* entity, WarStateBase* state);
WarOrderResult wos_tick(WarContext* context, WarEntity* entity,
                       void (*action)(WarContext*, WarEntity*));
void wos_remove(WarContext* context, WarEntity* entity);
void wos_beginDeath(WarContext* context, WarEntity* entity);
bool wos_deathTick(WarContext* context, u16 slot);
void wos_explore(WarContext* context, WarEntity* entity);
void wos_refreshVisibility(WarContext* context);
bool wos_attackZone(WarContext* context, s16* x, s16* y);
bool wos_areaTarget(WarContext* context, WarEntity* entity, s16 tx, s16 ty, s16* x, s16* y);
WarOrderResult wos_takePost(WarContext* context, WarEntity* entity, bool* alerted);
bool wos_validateTarget(WarContext* context, WarEntity* entity,
                       WarEntityId* target, WarEntityId* attacker);
