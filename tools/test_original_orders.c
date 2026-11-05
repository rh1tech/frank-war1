// Synthetic scripts and unit records, not game data.
// SPDX-License-Identifier: Zlib
#include <assert.h>
#include <string.h>

#include "../port/war_orders_frank.h"

typedef struct
{
    WarOriginalUnit units[WOR_UNIT_COUNT];
    uint8_t scripts[512];
    WarOriginalRuntime runtime;
    unsigned draws, orders, actions, selections, visibility, sounds, errors;
    unsigned summons, summonSuccesses;
    uint8_t lastSound;
    int issueTo;
    bool queueInHandler, consumeNext, handlerReturns;
} Fixture;

static uint16_t randomValue(WarOriginalRuntime* runtime)
{
    Fixture* fixture = runtime->context;
    fixture->draws++;
    return 7;
}

static WarOrderResult handleOrder(WarOriginalRuntime* runtime, uint16_t slot)
{
    Fixture* fixture = runtime->context;
    fixture->orders++;
    if (fixture->issueTo >= 0 && slot == 0)
    {
        assert(wor_beginCommand(runtime, (uint16_t)fixture->issueTo, 0, 20, 21) == WOR_CHANGED);
        assert(wor_queue(runtime, (uint16_t)fixture->issueTo, 3, 6) == WOR_CHANGED);
    }
    if (fixture->queueInHandler)
        assert(wor_queue(runtime, slot, 6, WOR_NONE) == WOR_CHANGED);
    if (fixture->consumeNext) return wor_consume(runtime, slot, true);
    return fixture->handlerReturns ? WOR_CHANGED : WOR_UNCHANGED;
}

static WarOrderResult handleAction(WarOriginalRuntime* runtime, uint16_t slot)
{
    Fixture* fixture = runtime->context;
    assert(runtime->units[slot].invisibility == 0);
    fixture->actions++;
    return WOR_UNCHANGED;
}

static void selected(WarOriginalRuntime* runtime, uint16_t slot)
{
    Fixture* fixture = runtime->context;
    assert(runtime->units[slot].wait >= 1);
    fixture->selections++;
}

static void visibility(WarOriginalRuntime* runtime, uint16_t slot)
{
    (void)slot;
    Fixture* fixture = runtime->context;
    fixture->visibility++;
}

static void sound(WarOriginalRuntime* runtime, uint16_t slot, uint8_t value)
{
    Fixture* fixture = runtime->context;
    assert(runtime->units[slot].sound == 0);
    fixture->lastSound = value;
    fixture->sounds++;
}

static void error(WarOriginalRuntime* runtime, uint16_t slot, const char* message)
{
    (void)slot;
    assert(message && *message);
    Fixture* fixture = runtime->context;
    fixture->errors++;
}

static void putWord(uint8_t* bytes, unsigned offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
}

static void initialize(Fixture* fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    wor_clearUnits(fixture->units);
    fixture->issueTo = -1;
    fixture->runtime = (WarOriginalRuntime){
        .units = fixture->units, .scripts = fixture->scripts,
        .scriptLength = sizeof(fixture->scripts), .context = fixture,
        .random = randomValue, .order = handleOrder, .action = handleAction,
        .visibility = visibility, .sound = sound, .selected = selected, .error = error
    };
    for (unsigned type = 0; type <= 53; type++) putWord(fixture->scripts, type * 2, 128);
    for (unsigned animation = 0; animation < 6; animation++)
    {
        const uint16_t start = (uint16_t)(192 + animation * 16);
        putWord(fixture->scripts, 128 + animation * 2, start);
        const uint8_t program[] = { 0, 1, 2, 3, (uint8_t)start, (uint8_t)(start >> 8) };
        memcpy(fixture->scripts + start, program, sizeof(program));
    }
    fixture->units[0].flags = 0;
    assert(wor_replace(&fixture->runtime, 0, 2) == WOR_CHANGED);
    fixture->selections = 0;
}

static void pendingAndNext(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalRuntime* runtime = &fixture.runtime;
    WarOriginalUnit* unit = &fixture.units[0];
    assert(wor_queue(runtime, 0, 3, 6) == WOR_CHANGED);
    assert(unit->current == 2 && unit->wait == 1);
    assert(wor_unitTick(runtime, 0, 0, false) == WOR_CHANGED);
    assert(unit->current == 3 && unit->pending == WOR_NONE && unit->next == 6);
    assert(fixture.orders == 0 && fixture.visibility == 1 && fixture.selections == 1);
    assert(wor_unitTick(runtime, 0, 0, false) == WOR_UNCHANGED);
    assert(fixture.orders == 1 && unit->current == 3 && unit->next == 6);
    assert(wor_consume(runtime, 0, true) == WOR_CHANGED);
    assert(unit->current == 6 && unit->next == WOR_NONE);

    assert(wor_queue(runtime, 0, 3, 8) == WOR_CHANGED);
    assert(wor_queue(runtime, 0, 2, WOR_NONE) == WOR_CHANGED);
    unit->target = 2;
    assert(wor_consume(runtime, 0, false) == WOR_CHANGED);
    assert(unit->current == 2 && unit->target == 0 && unit->next == WOR_NONE);
    assert(wor_replace(runtime, 0, 2) == WOR_CHANGED);
    assert(unit->pc == 224 && unit->wait == 1);
    assert(fixture.errors == 0);
}

static void handlerConsumption(void)
{
    Fixture fixture;
    initialize(&fixture);
    fixture.queueInHandler = true;
    assert(wor_unitTick(&fixture.runtime, 0, 0, false) == WOR_UNCHANGED);
    assert(fixture.units[0].current == 2 && fixture.units[0].pending == 6);
    assert(fixture.orders == 1);

    initialize(&fixture);
    fixture.queueInHandler = fixture.handlerReturns = true;
    assert(wor_unitTick(&fixture.runtime, 0, 0, false) == WOR_CHANGED);
    assert(fixture.units[0].current == 6 && fixture.units[0].pending == WOR_NONE);
    assert(fixture.orders == 1 && fixture.selections == 1 && fixture.units[0].wait == 1);

    initialize(&fixture);
    fixture.units[0].next = 8;
    fixture.handlerReturns = true;
    assert(wor_unitTick(&fixture.runtime, 0, 0, false) == WOR_UNCHANGED);
    assert(fixture.units[0].current == 2 && fixture.units[0].next == 8);
    fixture.units[0].pc = 224;
    fixture.consumeNext = true;
    wor_unitTick(&fixture.runtime, 0, 0, false);
    assert(fixture.units[0].current == 8 && fixture.units[0].next == WOR_NONE);
}

static void guardedOrdersAndDraws(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalRuntime* runtime = &fixture.runtime;
    WarOriginalUnit* unit = &fixture.units[0];
    const uint16_t inactive[] = { WOR_FREE, WOR_DYING, WOR_REMOVED, WOR_PLACEHOLDER };
    for (unsigned i = 0; i < sizeof(inactive) / sizeof(*inactive); i++)
    {
        unit->flags = inactive[i];
        unit->pending = 6;
        assert(wor_queue(runtime, 0, 3, 8) == WOR_UNCHANGED);
        assert(wor_consume(runtime, 0, true) == WOR_UNCHANGED);
        assert(wor_replace(runtime, 0, 1) == WOR_UNCHANGED);
        assert(unit->current == 2 && unit->pending == 6);
    }
    unit->flags = WOR_INSIDE;
    assert(wor_replace(runtime, 0, 12) == WOR_CHANGED);
    assert(wor_queue(runtime, 0, 2, WOR_NONE) == WOR_UNCHANGED);
    assert(wor_beginCommand(runtime, 0, 0, 1, 2) == WOR_UNCHANGED);
    assert(wor_replace(runtime, 0, 13) == WOR_CHANGED);
    unit->flags |= WOR_RANDOM_ENTRY | WOR_SUPPRESS_ACTION;
    unit->pending = 6;
    assert(wor_consume(runtime, 0, false) == WOR_CHANGED);
    assert(fixture.draws == 1 && unit->wait == 8 && unit->flags == WOR_INSIDE);
    unit->flags |= WOR_RANDOM_ENTRY | WOR_SUPPRESS_ACTION;
    assert(wor_replace(runtime, 0, 5) == WOR_CHANGED);
    assert(fixture.draws == 1 && unit->wait == 1 && unit->flags == WOR_INSIDE);
    unit->flags |= WOR_RANDOM_ENTRY | WOR_SUPPRESS_ACTION;
    assert(wor_queue(runtime, 0, 8, WOR_NONE) == WOR_CHANGED);
    assert((unit->flags & WOR_RANDOM_ENTRY) == 0 && (unit->flags & WOR_SUPPRESS_ACTION));
    assert(wor_consume(runtime, 0, false) == WOR_CHANGED);
    assert(fixture.draws == 1 && unit->wait == 1);
}

static void commandDuringScript(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalRuntime* runtime = &fixture.runtime;
    WarOriginalUnit* unit = &fixture.units[0];
    fixture.units[1].flags = 0;
    fixture.units[1].tileX = 17;
    fixture.units[1].tileY = 23;
    assert(wor_replace(runtime, 0, 6) == WOR_CHANGED);
    const uint8_t program[] = { 6, 254, 9, 8, 9, 0 };
    memcpy(fixture.scripts + 400, program, sizeof(program));
    unit->pc = 400;
    unit->pixelX = unit->pixelY = 100;
    unit->direction = 2;
    unit->sound = 42;
    unit->invisibility = 80;
    assert(wor_beginCommand(runtime, 0, 2, 99, 99) == WOR_CHANGED);
    assert(unit->goalX == 17 && unit->goalY == 23 && unit->target == 2 && !unit->sound);
    assert(wor_queue(runtime, 0, 3, 8) == WOR_CHANGED);
    assert(wor_unitTick(runtime, 0, 0, false) == WOR_CHANGED);
    assert(unit->pixelX == 98 && unit->pixelY == 100 && unit->frame == 9);
    assert(fixture.actions == 0 && fixture.sounds == 1 && fixture.lastSound == 0);
    assert(unit->invisibility == 80 && unit->current == 3 && unit->next == 8);
    assert((unit->flags & WOR_SUPPRESS_ACTION) == 0);
    assert(wor_replace(runtime, 0, 6) == WOR_CHANGED);
    unit->pc = 404;
    assert(wor_scriptTick(runtime, 0, 0, false) == WOR_CHANGED);
    assert(fixture.actions == 1 && unit->invisibility == 0);
}

static void heldAndPlaceholderTicks(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalUnit* unit = &fixture.units[0];
    unit->flags = WOR_PLACEHOLDER;
    unit->placeholderTicks = 2;
    unit->timer = 3;
    unit->wait = 9;
    assert(wor_unitTick(&fixture.runtime, 0, 0, false) == WOR_UNCHANGED);
    assert(unit->placeholderTicks == 1 && unit->timer == 3 && unit->wait == 9);
    unit->flags = WOR_HELD;
    wor_unitTick(&fixture.runtime, 0, 0, false);
    assert(unit->timer == 2 && unit->wait == 9 && fixture.orders == 0);
    unit->flags |= WOR_HOLD_REQUEST;
    wor_unitTick(&fixture.runtime, 0, 0, false);
    assert(unit->timer == 1 && unit->wait == 9 && fixture.orders == 1 && fixture.visibility == 0);

    unit->flags = WOR_HOLD_REQUEST;
    unit->wait = 1;
    assert(wor_queue(&fixture.runtime, 0, 3, 6) == WOR_CHANGED);
    wor_unitTick(&fixture.runtime, 0, 0, false);
    assert(unit->flags == WOR_HELD && unit->pending == 3 && fixture.orders == 1);
    assert(fixture.visibility == 1);
    unit->flags |= WOR_HOLD_REQUEST;
    assert(wor_unitTick(&fixture.runtime, 0, 0, false) == WOR_CHANGED);
    assert(unit->current == 3 && fixture.orders == 1 && fixture.visibility == 1);
}

static void targetAndAttackerIdentity(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalRuntime* runtime = &fixture.runtime;
    WarOriginalUnit* unit = &fixture.units[0];
    fixture.units[1].flags = fixture.units[2].flags = 0;
    fixture.units[1].owner = fixture.units[2].owner = 1;
    wor_noteAttacker(runtime, 0, 1);
    wor_noteAttacker(runtime, 0, 2);
    assert(unit->attacker == 2);
    fixture.units[1].flags = WOR_INSIDE;
    fixture.units[1].invisibility = 100;
    wor_noteAttacker(runtime, 0, 2);
    assert(unit->attacker == 2);
    unit->target = 3;
    assert(wor_validateTarget(runtime, 0) && unit->attacker == 0);
    wor_noteAttacker(runtime, 0, 1);
    unit->target = 2;
    assert(!wor_validateTarget(runtime, 0) && unit->target == 0 && unit->attacker == 0);
    unit->attacker = 2;
    assert(wor_validateTarget(runtime, 0) && unit->attacker == 2);
    fixture.units[1].flags = WOR_FREE;
    wor_noteAttacker(runtime, 0, 2);
    assert(unit->attacker == 3);
    fixture.units[2].owner = 0; // record identity survives ownership changes
    fixture.units[1].flags = 0;
    wor_noteAttacker(runtime, 0, 1);
    assert(unit->attacker == 3);
}

static void slotOrderAndFailures(void)
{
    Fixture fixture;
    initialize(&fixture);
    fixture.units[1].flags = 0;
    assert(wor_replace(&fixture.runtime, 1, 2) == WOR_CHANGED);
    fixture.issueTo = 1;
    for (uint16_t slot = 0; slot < WOR_UNIT_COUNT; slot++)
        assert(wor_unitTick(&fixture.runtime, slot, 0, false) != WOR_ERROR);
    assert(fixture.orders == 1 && fixture.units[1].current == 3 && fixture.units[1].wait == 1);

    fixture.runtime.scriptLength = 1;
    const uint8_t current = fixture.units[0].current;
    assert(wor_replace(&fixture.runtime, 0, 6) == WOR_ERROR);
    assert(fixture.units[0].current == current && fixture.errors == 1);
    assert(wor_queue(&fixture.runtime, 0, 32, WOR_NONE) == WOR_ERROR);
    assert(wor_unitTick(&fixture.runtime, WOR_UNIT_COUNT, 0, false) == WOR_ERROR);
    assert(fixture.errors == 3);
}

static void deathAndCorpseScripts(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalRuntime* runtime = &fixture.runtime;
    WarOriginalUnit* unit = &fixture.units[0];
    assert(wor_replace(runtime, 0, 1) == WOR_CHANGED);
    unit->flags = WOR_DYING | 0x400;
    unit->owner = 1;
    assert(wor_finishDeath(runtime, 0) == WOR_CHANGED);
    assert(unit->type == 51 && unit->owner == 4 && unit->current == 1);
    assert(unit->flags == (WOR_DYING | 0x400) && unit->pc == 208 && unit->wait == 1);
    assert(wor_scriptTick(runtime, 0, 0, false) == WOR_CHANGED);
    assert(wor_finishDeath(runtime, 0) == WOR_CHANGED);
    assert(unit->current == 0 && unit->flags == (WOR_REMOVED | 0x400));
    assert(unit->pc == 192 && unit->wait == 1);

    initialize(&fixture);
    unit->type = 14;
    assert(wor_replace(runtime, 0, 1) == WOR_CHANGED);
    unit->flags = WOR_DYING;
    assert(wor_finishDeath(runtime, 0) == WOR_CHANGED);
    assert(unit->type == 51 && unit->pc == 224 && unit->current == 1);

    initialize(&fixture);
    unit->type = 4;
    assert(wor_replace(runtime, 0, 1) == WOR_CHANGED);
    unit->flags = WOR_DYING;
    assert(wor_finishDeath(runtime, 0) == WOR_CHANGED);
    assert(unit->type == 4 && unit->current == 0 && unit->flags == WOR_REMOVED);
    assert(wor_selectAnimation(runtime, 0, 6) == WOR_ERROR);
    assert(fixture.errors == 1 && fixture.draws == 0);
}

static void checkpointExploration(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalVision vision = {0};
    WarOriginalRuntime* runtime = &fixture.runtime;
    WarOriginalUnit* unit = &fixture.units[0];
    unit->type = 6;
    unit->tileX = 12;
    unit->tileY = 9;
    assert(wor_explore(runtime, &vision, 0, 1, 1, true, false) == WOR_CHANGED);
    assert(vision.tiles[9 * 64 + 15] == 1);
    assert(vision.tiles[11 * 64 + 16] == 0);
    fixture.units[1] = (WarOriginalUnit){ .tileX = 15, .tileY = 9, .owner = 1 };
    wor_refreshVisibility(runtime, &vision, 1, 1, 1, false);
    assert(fixture.units[1].visibility == 0x200);
    unit->tileX = 40;
    wor_explore(runtime, &vision, 0, 1, 1, true, false);
    assert(vision.tiles[9 * 64 + 40] == 0); // A repeated stationary checkpoint has no new reveal.
    unit->current = 3;
    wor_explore(runtime, &vision, 0, 1, 1, true, false);
    assert(vision.tiles[9 * 64 + 42] == 1 && vision.tiles[9 * 64 + 43] == 0);
    unit->current = 2;
    wor_explore(runtime, &vision, 0, 1, 1, true, false);
    assert(vision.tiles[9 * 64 + 43] == 1 && vision.tiles[9 * 64 + 15] == 1);

    unit->owner = 1;
    unit->flags = 0;
    unit->tileX = unit->tileY = 0;
    unit->current = 6;
    unit->target = 2;
    fixture.units[1].owner = 0;
    wor_explore(runtime, &vision, 0, 1, 1, false, false);
    assert(vision.tiles[0] == 1 && vision.tiles[2 * 64 + 2] == 0);
    fixture.units[1].flags = WOR_INSIDE;
    wor_refreshVisibility(runtime, &vision, 1, 1, 1, true);
    assert(fixture.units[1].visibility == 0x300);
    assert(fixture.draws == 0 && fixture.errors == 0);
}

static void attackZones(void)
{
    Fixture fixture;
    initialize(&fixture);
    wor_clearUnits(fixture.units);
    int16_t x, y;
    assert(wor_attackZone(&fixture.runtime, &x, &y) == WOR_CHANGED);
    assert(x == 8 && y == 8);
    fixture.units[0] = (WarOriginalUnit){ .type = 34, .tileX = 31, .tileY = 31 };
    fixture.units[1] = (WarOriginalUnit){ .type = 32, .tileX = 40, .tileY = 40 };
    wor_attackZone(&fixture.runtime, &x, &y);
    assert(x == 24 && y == 24);
    fixture.units[2] = (WarOriginalUnit){ .type = 2, .tileX = 40, .tileY = 40, .flags = WOR_INSIDE };
    wor_attackZone(&fixture.runtime, &x, &y);
    assert(x == 40 && y == 40);
    fixture.units[2].flags = WOR_DYING;
    wor_attackZone(&fixture.runtime, &x, &y);
    assert(x == 24 && y == 24 && fixture.errors == 0);
}

static void invisibilityLifecycle(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalRuntime* runtime = &fixture.runtime;
    WarOriginalUnit* caster = &fixture.units[0];
    WarOriginalUnit* recipient = &fixture.units[1];
    caster->target = 2;
    recipient->flags = 0;
    uint8_t mana = 124;
    assert(wor_invisibility(runtime, 0, &mana, false) == WOR_UNCHANGED);
    assert(mana == 124 && recipient->invisibility == 0);
    mana = 250;
    assert(wor_invisibility(runtime, 0, &mana, false) == WOR_CHANGED);
    assert(mana == 250 && recipient->invisibility == 0);
    assert(wor_invisibility(runtime, 0, &mana, true) == WOR_CHANGED);
    wor_tickSpellTimers(runtime);
    assert(mana == 125 && recipient->invisibility == 2999);
    assert(!wor_validateTarget(runtime, 0));
    assert(caster->target == 0);
    recipient->flags = WOR_DYING;
    wor_tickSpellTimers(runtime);
    assert(recipient->invisibility == 2999);
    recipient->flags = WOR_INSIDE;
    recipient->invisibility = 1;
    wor_tickSpellTimers(runtime);
    wor_tickSpellTimers(runtime);
    assert(recipient->invisibility == 0 && fixture.draws == 0 && fixture.errors == 0);
}

static void manaRecovery(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalUnit* caster = &fixture.units[0];
    caster->type = 10;
    caster->statusTicks = 40;
    uint8_t mana = 254;
    for (unsigned tick = 0; tick < 40; tick++)
        wor_regenerateMana(&fixture.runtime, 0, 1, &mana);
    assert(mana == 255 && caster->statusTicks == 60);
    for (unsigned tick = 0; tick < 120; tick++)
        wor_regenerateMana(&fixture.runtime, 0, 1, &mana);
    assert(mana == 255 && caster->statusTicks == 60);
    mana -= 125;
    for (unsigned tick = 0; tick < 59; tick++)
        wor_regenerateMana(&fixture.runtime, 0, 1, &mana);
    assert(mana == 130 && caster->statusTicks == 1);
    assert(wor_regenerateMana(&fixture.runtime, 0, 1, &mana) == WOR_CHANGED);
    assert(mana == 131 && caster->statusTicks == 60 && fixture.draws == 0 && fixture.errors == 0);
}

static bool summon(WarOriginalRuntime* runtime, uint16_t caster, uint8_t type)
{
    Fixture* fixture = runtime->context;
    assert(caster == 0 && type == 24);
    fixture->summons++;
    if (fixture->summons == 1) return false;
    fixture->summonSuccesses++;
    return true;
}

static void summoningAttempts(void)
{
    Fixture fixture;
    initialize(&fixture);
    fixture.units[0].current = 25;
    fixture.runtime.summon = summon;
    uint8_t mana = 255;
    assert(wor_summon(&fixture.runtime, 0, &mana, false) == WOR_CHANGED);
    assert(mana == 255 && fixture.summons == 0);
    assert(wor_summon(&fixture.runtime, 0, &mana, true) == WOR_CHANGED);
    assert(mana == 75 && fixture.summons == 4 && fixture.summonSuccesses == 3);
    assert(wor_summon(&fixture.runtime, 0, &mana, true) == WOR_CHANGED);
    assert(mana == 15 && fixture.summons == 5 && fixture.summonSuccesses == 4);
    assert(wor_summon(&fixture.runtime, 0, &mana, true) == WOR_UNCHANGED);
    assert(fixture.summons == 5 && fixture.errors == 0 && fixture.draws == 0);
}

static void summonDecay(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalUnit* unit = &fixture.units[0];
    unit->type = 24;
    unit->statusTicks = 1;
    unit->flags = 0x400;
    uint8_t mana = 1;
    uint16_t health = 5;
    bool dead = true;
    assert(wor_decay(&fixture.runtime, 0, &mana, &health, &dead) == WOR_UNCHANGED);
    assert(mana == 1 && health == 5 && unit->statusTicks == 1 && !dead);
    unit->flags = WOR_INSIDE;
    assert(wor_decay(&fixture.runtime, 0, &mana, &health, &dead) == WOR_CHANGED);
    assert(mana == 0 && health == 5 && unit->statusTicks == 45 && !dead);
    for (unsigned tick = 0; tick < 45; tick++)
        wor_decay(&fixture.runtime, 0, &mana, &health, &dead);
    assert(health == 0 && !dead);
    for (unsigned tick = 0; tick < 44; tick++)
        wor_decay(&fixture.runtime, 0, &mana, &health, &dead);
    assert(!dead);
    assert(wor_decay(&fixture.runtime, 0, &mana, &health, &dead) == WOR_CHANGED);
    assert(dead && unit->statusTicks == 45 && fixture.draws == 0 && fixture.errors == 0);
}

static bool resurrect(WarOriginalRuntime* runtime, uint16_t caster, uint16_t corpse)
{
    Fixture* fixture = runtime->context;
    assert(caster == 0 && corpse == fixture->summons + 1);
    fixture->summons++;
    if (corpse == 1) return false;
    fixture->summonSuccesses++;
    return true;
}

static void resurrectionAttempts(void)
{
    Fixture fixture;
    initialize(&fixture);
    fixture.runtime.resurrect = resurrect;
    fixture.units[0].goalX = fixture.units[0].goalY = 50;
    for (unsigned i = 1; i <= 4; i++)
        fixture.units[i] = (WarOriginalUnit){
            .type = 51, .flags = WOR_DYING, .tileX = (int16_t)i
        };
    assert(wor_findCorpse(&fixture.runtime, 0) == 2);
    uint8_t mana = 140;
    assert(wor_resurrect(&fixture.runtime, 0, &mana, false) == WOR_CHANGED);
    assert(mana == 140 && fixture.summons == 0);
    assert(wor_resurrect(&fixture.runtime, 0, &mana, true) == WOR_CHANGED);
    assert(mana == 0 && fixture.summons == 3 && fixture.summonSuccesses == 2);
    assert(fixture.units[1].flags == WOR_DYING && fixture.units[4].flags == WOR_DYING);
    assert(fixture.units[2].flags == (WOR_DYING | WOR_INSIDE));
    assert(fixture.units[3].flags == (WOR_DYING | WOR_INSIDE));
    fixture.units[1].tileX = 10;
    assert(wor_findCorpse(&fixture.runtime, 0) == 5);
    fixture.units[4].flags |= WOR_REMOVED;
    assert(wor_findCorpse(&fixture.runtime, 0) == 0);
    assert(fixture.errors == 0 && fixture.draws == 0);
}

static void shotAffordability(void)
{
    Fixture fixture;
    initialize(&fixture);
    fixture.units[0].type = 14;
    uint8_t mana = 4;
    assert(wor_shotMana(&fixture.runtime, 0, 0, &mana, false) == WOR_UNCHANGED);
    assert(mana == 4);
    assert(wor_shotMana(&fixture.runtime, 0, 2, &mana, false) == WOR_CHANGED);
    assert(mana == 4);
    assert(wor_shotMana(&fixture.runtime, 0, 2, &mana, true) == WOR_CHANGED);
    assert(mana == 3);
    mana = 5;
    assert(wor_shotMana(&fixture.runtime, 0, 0, &mana, false) == WOR_CHANGED);
    assert(wor_shotMana(&fixture.runtime, 0, 0, &mana, true) == WOR_CHANGED);
    assert(mana == 0 && fixture.draws == 0 && fixture.errors == 0);
}

static uint16_t poisonOccupant(WarOriginalRuntime* runtime, int16_t x, int16_t y)
{
    (void)runtime;
    if (y == 10 && x == 10) return 1;
    if (y == 10 && x == 9) return 2;
    if (y == 11 && (x == 10 || x == 11)) return 3;
    return 0;
}

static void poisonDamage(WarOriginalRuntime* runtime, uint16_t source, uint16_t target, uint8_t amount)
{
    Fixture* fixture = runtime->context;
    assert(source == 1 && (target == 2 || target == 3));
    assert(amount == (target == 2 ? 5 : 1));
    fixture->actions++;
}

static void poisonLifecycle(void)
{
    Fixture fixture;
    initialize(&fixture);
    WarOriginalEffect effects[WOR_EFFECT_COUNT] = {0};
    fixture.runtime.effects = effects;
    fixture.runtime.occupant = poisonOccupant;
    fixture.runtime.damage = poisonDamage;
    fixture.units[0].goalX = fixture.units[0].goalY = 10;
    fixture.units[2].type = 32;
    putWord(fixture.scripts, 104 + 3 * 2, 128);
    uint8_t mana = 49;
    assert(wor_poison(&fixture.runtime, 0, &mana, true) == WOR_UNCHANGED);
    assert(mana == 49 && !effects[0].used);
    mana = 100;
    for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++) effects[i].used = true;
    assert(wor_poison(&fixture.runtime, 0, &mana, true) == WOR_CHANGED && mana == 50);
    effects[17].used = false;
    assert(wor_poison(&fixture.runtime, 0, &mana, false) == WOR_CHANGED);
    assert(mana == 50 && !effects[17].used);
    assert(wor_poison(&fixture.runtime, 0, &mana, true) == WOR_CHANGED && mana == 0);
    WarOriginalEffect* effect = &effects[17];
    assert(effect->used && effect->pulses == 25 && fixture.actions == 0);
    assert(wor_poisonTick(&fixture.runtime, effect) == WOR_CHANGED && fixture.actions == 0);
    unsigned ticks = 0;
    while (effect->used && ticks++ < 1000) assert(wor_poisonTick(&fixture.runtime, effect) != WOR_ERROR);
    assert(!effect->used && fixture.actions == 25 * 3);
    mana = 50;
    assert(wor_poison(&fixture.runtime, 0, &mana, true) == WOR_CHANGED);
    assert(wor_poisonTick(&fixture.runtime, effect) == WOR_CHANGED);
    fixture.runtime.windX = -2;
    effect->x = 1;
    const unsigned hits = fixture.actions;
    assert(wor_poisonTick(&fixture.runtime, effect) == WOR_CHANGED);
    assert(!effect->used && effect->pulses == 25 && fixture.actions == hits);
    assert(fixture.errors == 0 && fixture.draws == 0);
}

int main(void)
{
    pendingAndNext();
    handlerConsumption();
    guardedOrdersAndDraws();
    commandDuringScript();
    heldAndPlaceholderTicks();
    targetAndAttackerIdentity();
    slotOrderAndFailures();
    deathAndCorpseScripts();
    checkpointExploration();
    attackZones();
    invisibilityLifecycle();
    manaRecovery();
    summoningAttempts();
    resurrectionAttempts();
    summonDecay();
    shotAffordability();
    poisonLifecycle();
    return 0;
}
