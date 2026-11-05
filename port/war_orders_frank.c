// Source contracts and migration boundaries are recorded in docs/ai.md.
// SPDX-License-Identifier: Zlib
#include "war_orders_frank.h"
#include "war_script_frank.h"

#include <string.h>

static WarOrderResult wor_error(WarOriginalRuntime* runtime, uint16_t slot, const char* message)
{
    runtime->error(runtime, slot, message);
    return WOR_ERROR;
}

bool wor_areEnemies(uint8_t first, uint8_t second)
{
    return first < 4 && second < 4 && ((first == 0) != (second == 0));
}

// Tile facing uses half the larger displacement, rounded down but at least
// one, as its diagonal threshold. Coincident tiles face east (0x1DF50).
uint8_t wor_facing(int16_t dx, int16_t dy)
{
    int x = dx < 0 ? -dx : dx, y = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    if (x >= y)
    {
        const int threshold = x / 2 > 0 ? x / 2 : 1;
        if (y < threshold) sy = 0;
    }
    else
    {
        const int threshold = y / 2 > 0 ? y / 2 : 1;
        if (x < threshold) sx = 0;
    }
    if (sy < 0) return sx < 0 ? 7 : sx > 0 ? 1 : 0;
    if (sy > 0) return sx < 0 ? 5 : sx > 0 ? 3 : 4;
    return sx < 0 ? 6 : 2;
}

// Clockwise expanding rectangles begin at the corner facing the requested
// destination. Failure leaves the original coordinates intact (0x2E554).
WarOrderResult wor_findExit(WarOriginalRuntime* runtime, int16_t* x, int16_t* y,
                           uint8_t width, uint8_t height, int16_t towardX, int16_t towardY)
{
    if (!runtime->blocked || !x || !y || !width || !height)
        return wor_error(runtime, WOR_UNIT_COUNT, "invalid exit search");
    const bool right = towardX > *x, below = towardY > *y;
    const unsigned first = below ? (right ? 2 : 3) : (right ? 1 : 0);
    int cx = *x + (right ? width : -1), cy = *y + (below ? height : -1);
    unsigned w = width, h = height;
    while (w < 12)
    {
        w += 2;
        h += 2;
        for (unsigned side = 0; side < 4; side++)
        {
            const unsigned direction = (first + side) & 3;
            const int dx = direction == 0 ? 1 : direction == 2 ? -1 : 0;
            const int dy = direction == 1 ? 1 : direction == 3 ? -1 : 0;
            const unsigned length = direction & 1 ? h : w;
            for (unsigned step = 0; step < length; step++)
            {
                if ((unsigned)cx < WOR_MAP_WIDTH && (unsigned)cy < WOR_MAP_WIDTH &&
                    !runtime->blocked(runtime, (int16_t)cx, (int16_t)cy))
                {
                    *x = (int16_t)cx;
                    *y = (int16_t)cy;
                    return WOR_CHANGED;
                }
                cx += dx;
                cy += dy;
            }
            cx -= dx;
            cy -= dy;
        }
        cx += right ? 1 : -1;
        // The original shifts downward only from the upper-right corner.
        cy += first == 1 ? 1 : -1;
    }
    return WOR_UNCHANGED;
}

static bool wor_mobile(const WarOriginalUnit* unit)
{
    return unit->type < 32;
}

// Safety counts occupied tiles, including neutral units and each building tile (0x11140).
WarOrderResult wor_checkCatapult(WarOriginalRuntime* runtime, uint16_t slot,
                                uint8_t controller, uint8_t minimumRange,
                                uint8_t targetWidth, uint8_t targetHeight)
{
    if (slot >= WOR_UNIT_COUNT || !targetWidth || !targetHeight)
        return wor_error(runtime, slot, "invalid catapult record");
    WarOriginalUnit* unit = &runtime->units[slot];
    if (!unit->target || unit->target > WOR_UNIT_COUNT)
        return wor_error(runtime, slot, "missing catapult target");
    const WarOriginalUnit* target = &runtime->units[unit->target - 1];
    const int right = target->tileX + targetWidth - 1, bottom = target->tileY + targetHeight - 1;
    const int dx = unit->tileX < target->tileX ? target->tileX - unit->tileX
        : unit->tileX > right ? unit->tileX - right : 0;
    const int dy = unit->tileY < target->tileY ? target->tileY - unit->tileY
        : unit->tileY > bottom ? unit->tileY - bottom : 0;
    if (dx < minimumRange && dy < minimumRange)
        return wor_queue(runtime, slot, 2, WOR_NONE);
    if (!runtime->occupant) return wor_error(runtime, slot, "missing occupancy callback");
    unsigned remaining = controller == 0 ? 0 : 2;
    for (int y = target->tileY - 2; y <= target->tileY + 2; y++)
        for (int x = target->tileX - 2; x <= target->tileX + 2; x++)
        {
            if ((unsigned)x >= WOR_MAP_WIDTH || (unsigned)y >= WOR_MAP_WIDTH) continue;
            const uint16_t reference = runtime->occupant(runtime, (int16_t)x, (int16_t)y);
            if (reference > WOR_UNIT_COUNT) return wor_error(runtime, slot, "invalid occupied slot");
            if (!reference || wor_areEnemies(unit->owner, runtime->units[reference - 1].owner)) continue;
            if (!remaining) return wor_queue(runtime, slot, 2, WOR_NONE);
            remaining--;
        }
    return WOR_UNCHANGED;
}

static uint16_t wor_readWord(const uint8_t* data)
{
    return (uint16_t)(data[0] | (uint16_t)data[1] << 8);
}

// The first hostile occupied tile wins, even if its building begins
// outside the scan. Aim uses that record's origin and two draws (0x2A864).
WarOrderResult wor_areaTarget(WarOriginalRuntime* runtime, uint16_t slot,
                              int16_t targetX, int16_t targetY, int16_t* x, int16_t* y)
{
    if (slot >= WOR_UNIT_COUNT || !runtime->occupant || !runtime->random || !x || !y)
        return wor_error(runtime, slot, "invalid area target search");
    const WarOriginalUnit* caster = &runtime->units[slot];
    if (targetX == -1 || targetY == -1 ||
        targetX - caster->tileX > 25 || caster->tileX - targetX > 25 ||
        targetY - caster->tileY > 25 || caster->tileY - targetY > 25) return WOR_UNCHANGED;
    for (int cy = targetY - 5; cy <= targetY + 5; cy++)
        for (int cx = targetX - 5; cx <= targetX + 5; cx++)
        {
            if ((unsigned)cx >= WOR_MAP_WIDTH || (unsigned)cy >= WOR_MAP_WIDTH) continue;
            const uint16_t target = runtime->occupant(runtime, (int16_t)cx, (int16_t)cy);
            if (target > WOR_UNIT_COUNT) return wor_error(runtime, slot, "invalid area occupant");
            if (!target) continue;
            const WarOriginalUnit* unit = &runtime->units[target - 1];
            if ((unit->flags & WOR_INACTIVE) || !wor_areEnemies(caster->owner, unit->owner)) continue;
            *x = (int16_t)(unit->tileX + runtime->random(runtime) % 5 - 2);
            *y = (int16_t)(unit->tileY + runtime->random(runtime) % 5 - 2);
            return WOR_CHANGED;
        }
    return WOR_UNCHANGED;
}

// Wind changes after effects, before spell timers (0x2D3D0).
void wor_tickWind(WarOriginalRuntime* runtime, int8_t* x, int8_t* y)
{
    if (runtime->random(runtime) & 15) return;
    int8_t* axis = runtime->random(runtime) & 1 ? x : y;
    int value = *axis + runtime->random(runtime) % 3 - 1;
    *axis = (int8_t)(value < -2 ? -2 : value > 2 ? 2 : value);
}

static WarOrderResult wor_effectAnimation(WarOriginalRuntime* runtime, WarOriginalEffect* effect,
                                           uint8_t animation)
{
    const uint32_t index = 104u + effect->type * 2u;
    if (!runtime->scripts || index + 2 > runtime->scriptLength)
        return wor_error(runtime, WOR_UNIT_COUNT, "missing effect script table");
    const uint32_t table = (wor_readWord(runtime->scripts + index) / 2u + animation) * 2u;
    if (table + 2 > runtime->scriptLength)
        return wor_error(runtime, WOR_UNIT_COUNT, "invalid effect animation table");
    const uint16_t pc = wor_readWord(runtime->scripts + table);
    if (pc >= runtime->scriptLength)
        return wor_error(runtime, WOR_UNIT_COUNT, "invalid effect animation");
    effect->pc = pc;
    effect->wait = 1;
    return WOR_CHANGED;
}

// Payment precedes allocation; a full shared pool still costs fifty (0x27914).
WarOrderResult wor_poison(WarOriginalRuntime* runtime, uint16_t slot, uint8_t* mana, bool action)
{
    if (slot >= WOR_UNIT_COUNT || !mana)
        return wor_error(runtime, slot, "invalid poison caster");
    if (*mana < 50) return WOR_UNCHANGED;
    if (!action) return WOR_CHANGED;
    if (!runtime->effects) return wor_error(runtime, slot, "missing effect pool");
    *mana -= 50;
    for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++)
    {
        WarOriginalEffect* effect = &runtime->effects[i];
        if (effect->used) continue;
        const WarOriginalUnit* caster = &runtime->units[slot];
        *effect = (WarOriginalEffect){
            .used = true, .type = 3, .damage = 5, .pulses = 25, .source = slot + 1,
            .x = (int16_t)(caster->goalX * 16 + 8), .y = (int16_t)(caster->goalY * 16 + 8)
        };
        if (wor_effectAnimation(runtime, effect, 0) == WOR_ERROR) return WOR_ERROR;
        if (runtime->effectCreated) runtime->effectCreated(runtime, effect);
        break;
    }
    return WOR_CHANGED;
}

static WarOrderResult wor_effectScriptTick(WarOriginalRuntime* runtime, WarOriginalEffect* effect)
{
    if (!wscr_countdown(&effect->wait)) return WOR_UNCHANGED;
    uint32_t pc = effect->pc;
    for (unsigned n = 0; n < 64; n++)
    {
        WarScriptInstruction instruction;
        if (!wscr_decode(runtime->scripts, runtime->scriptLength, pc, 0, false, &instruction))
            return wor_error(runtime, WOR_UNIT_COUNT, "invalid effect instruction");
        pc = instruction.next;
        switch (instruction.opcode)
        {
            case 0: case 1: case 2: case 7: case 10:
                effect->pc = (uint16_t)pc;
                effect->wait = instruction.wait;
                return instruction.opcode == 0 ? WOR_CHANGED : WOR_UNCHANGED;
            case 3: break;
            case 4: effect->frame = instruction.frame; break;
            case 5: case 6:
            {
                const uint8_t direction = effect->direction & 7;
                const int dx = direction >= 1 && direction <= 3 ? 1 : direction >= 5 ? -1 : 0;
                const int dy = direction == 0 || direction == 1 || direction == 7 ? -1
                    : direction >= 3 && direction <= 5 ? 1 : 0;
                effect->x = (int16_t)(effect->x + dx * (int8_t)instruction.operand);
                effect->y = (int16_t)(effect->y + dy * (int8_t)instruction.operand);
                if (instruction.opcode == 6) effect->frame = instruction.frame;
                break;
            }
            case 8:
            {
                const uint8_t sound = effect->sound;
                effect->sound = 0;
                if (runtime->effectSound) runtime->effectSound(runtime, effect, sound);
                break;
            }
            default:
                return wor_error(runtime, WOR_UNIT_COUNT, "unit action in effect script");
        }
    }
    return wor_error(runtime, WOR_UNIT_COUNT, "effect script did not yield");
}

WarOrderResult wor_poisonTick(WarOriginalRuntime* runtime, WarOriginalEffect* effect)
{
    if (!effect->used) return WOR_UNCHANGED;
    if (effect->type != 3 || !runtime->occupant || !runtime->damage)
        return wor_error(runtime, WOR_UNIT_COUNT, "invalid poison effect");
    WarOrderResult result = wor_effectScriptTick(runtime, effect);
    if (result != WOR_CHANGED) return result;
    if (effect->state == 0)
    {
        effect->state = 3;
        return wor_effectAnimation(runtime, effect, 1);
    }
    if (effect->state != 3) return wor_error(runtime, WOR_UNIT_COUNT, "invalid poison phase");
    effect->x = (int16_t)(effect->x + runtime->windX);
    effect->y = (int16_t)(effect->y + runtime->windY);
    if ((uint16_t)effect->x >= 1024 || (uint16_t)effect->y >= 1024)
    {
        effect->used = false;
        return WOR_CHANGED;
    }
    const int cx = effect->x / 16, cy = effect->y / 16;
    for (int y = cy - 1; y <= cy + 1; y++)
        for (int x = cx - 1; x <= cx + 1; x++)
        {
            if ((unsigned)x >= WOR_MAP_WIDTH || (unsigned)y >= WOR_MAP_WIDTH) continue;
            const uint16_t target = runtime->occupant(runtime, (int16_t)x, (int16_t)y);
            if (target > WOR_UNIT_COUNT) return wor_error(runtime, WOR_UNIT_COUNT, "invalid poison occupant");
            if (!target || target == effect->source) continue;
            const uint8_t type = runtime->units[target - 1].type;
            runtime->damage(runtime, effect->source, target, type >= 32 && type <= 50 ? 1 : 5);
        }
    if ((int16_t)--effect->pulses <= 0) effect->used = false;
    return WOR_CHANGED;
}

// 0x511A4 and 0x531EC: the phase an effect takes at its first checkpoint,
// by type, and the animation each phase plays.
static const uint8_t wor_effectPhase[12] = { 1, 2, 1, 3, 4, 5, 5, 5, 5, 5, 1, 1 };
static const uint8_t wor_phaseAnimation[10] = { 0, 1, 1, 1, 1, 1, 2, 0, 3, 0 };

// 0x119C0: the first free record of the shared pool, cleared.
static WarOriginalEffect* wor_freeEffect(WarOriginalRuntime* runtime)
{
    for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++)
        if (!runtime->effects[i].used)
        {
            runtime->effects[i] = (WarOriginalEffect){ 0 };
            return &runtime->effects[i];
        }
    return NULL;
}

// 0x11E70: a short-lived visual (an impact: type 7) at a pixel, if the
// pool has room; it lives one animation (phases 0 and 5).
WarOrderResult wor_impactVisual(WarOriginalRuntime* runtime, uint8_t type, int16_t x, int16_t y)
{
    if (!runtime->effects) return wor_error(runtime, WOR_UNIT_COUNT, "missing effect pool");
    WarOriginalEffect* effect = wor_freeEffect(runtime);
    if (!effect) return WOR_UNCHANGED;
    effect->used = true;
    effect->type = type;
    effect->x = x;
    effect->y = y;
    if (wor_effectAnimation(runtime, effect, wor_phaseAnimation[0]) == WOR_ERROR) return WOR_ERROR;
    if (runtime->effectCreated) runtime->effectCreated(runtime, effect);
    return WOR_CHANGED;
}

// 0x120C8: damage to the width x height tiles round an effect's tile. With
// `quarter`, all but the middle take a quarter; a building takes a quarter
// of that again. The source is spared; an empty tile's terrain is struck
// when `terrain` is set.
void wor_areaDamage(WarOriginalRuntime* runtime, const WarOriginalEffect* effect, bool quarter,
                    int width, int height, bool terrain, uint8_t damage)
{
    const int cx = effect->x >= 0 ? effect->x / 16 : -((15 - effect->x) / 16);
    const int cy = effect->y >= 0 ? effect->y / 16 : -((15 - effect->y) / 16);
    const int sx = cx - width / 2, sy = cy - height / 2;
    for (int row = 0; row < height; row++)
        for (int column = 0; column < width; column++)
        {
            const int x = sx + column, y = sy + row;
            if ((unsigned)x >= WOR_MAP_WIDTH || (unsigned)y >= WOR_MAP_WIDTH) continue;
            uint8_t amount = damage;
            if (quarter && !(row == 1 && column == 1)) amount = (uint8_t)(amount / 4);
            const uint16_t target = runtime->occupant ? runtime->occupant(runtime, (int16_t)x, (int16_t)y) : 0;
            if (target)
            {
                const uint8_t type = runtime->units[target - 1].type;
                if (type >= 32 && type <= 50) amount = (uint8_t)(amount / 4);
                if (target != effect->source && runtime->damage)
                    runtime->damage(runtime, effect->source, target, amount);
            }
            else if (terrain && runtime->terrainDamage)
                runtime->terrainDamage(runtime, (int16_t)x, (int16_t)y, amount);
        }
}

// 0x585F0: the impact points of the last cast's four records, kept between
// casts (a record that found no room leaves its old point).
static int16_t wor_rainImpacts[4][2];

// 0x11CD8: one falling record at the caster's goal, off by whole tiles
// (-2..4) each way, a point unlike the cast's earlier records, starting 120
// pixels above it.
static WarOrderResult wor_rainRecord(WarOriginalRuntime* runtime, uint16_t slot, unsigned index)
{
    WarOriginalEffect* effect = wor_freeEffect(runtime);
    if (!effect) return WOR_UNCHANGED;
    const WarOriginalUnit* caster = &runtime->units[slot];
    *effect = (WarOriginalEffect){
        .used = true, .type = 4, .damage = 32, .source = slot + 1,
        .x = (int16_t)(caster->goalX * 16 + 8), .y = (int16_t)(caster->goalY * 16 + 8)
    };
    if (wor_effectAnimation(runtime, effect, wor_phaseAnimation[0]) == WOR_ERROR) return WOR_ERROR;
    int16_t x, y;
    for (;;)
    {
        x = (int16_t)(effect->x + (runtime->random(runtime) % 7) * 16 - 32);
        y = (int16_t)(effect->y + (runtime->random(runtime) % 7) * 16 - 32);
        unsigned k = 0;
        while (k < index && (wor_rainImpacts[k][0] != x || wor_rainImpacts[k][1] != y)) k++;
        if (k >= index) break;
    }
    wor_rainImpacts[index][0] = x;
    wor_rainImpacts[index][1] = y;
    effect->x = effect->goalX = x;
    effect->goalY = y;
    effect->y = (int16_t)(y - 120);
    effect->step = 0;
    if (runtime->effectCreated) runtime->effectCreated(runtime, effect);
    return WOR_CHANGED;
}

// 0x2793C: rain of fire. Unaffordable, it queues idle; the action pays
// fifty for each four records, and the order goes on (0x27BCC).
WarOrderResult wor_rain(WarOriginalRuntime* runtime, uint16_t slot, uint8_t* mana, bool action)
{
    if (slot >= WOR_UNIT_COUNT || !mana) return wor_error(runtime, slot, "invalid rain caster");
    if (*mana < 50)
    {
        if (wor_queue(runtime, slot, 2, WOR_NONE) == WOR_ERROR) return WOR_ERROR;
        return WOR_UNCHANGED;
    }
    if (!action) return WOR_CHANGED;
    if (!runtime->effects || !runtime->random) return wor_error(runtime, slot, "missing effect pool");
    *mana -= 50;
    for (unsigned i = 0; i < 4; i++)
        if (wor_rainRecord(runtime, slot, i) == WOR_ERROR) return WOR_ERROR;
    return WOR_CHANGED;
}

// One pool record's tick in the unit dispatch (0x2ECFC): its script, then
// its phase at a checkpoint: 0 picks the type's phase (0x11FA8), 4 is a
// falling record (0x12338), 5 releases a visual (0x123E0).
WarOrderResult wor_effectTick(WarOriginalRuntime* runtime, WarOriginalEffect* effect)
{
    if (!effect->used) return WOR_UNCHANGED;
    WarOrderResult result = wor_effectScriptTick(runtime, effect);
    if (result != WOR_CHANGED) return result;
    switch (effect->state)
    {
        case 0:
            if (effect->type >= sizeof(wor_effectPhase))
                return wor_error(runtime, WOR_UNIT_COUNT, "invalid effect type");
            effect->state = wor_effectPhase[effect->type];
            return wor_effectAnimation(runtime, effect, wor_phaseAnimation[effect->state]);
        case 4:
            if (effect->step == 3)
            {
                effect->used = false;
                return WOR_CHANGED;
            }
            if (effect->step & 0x80)
            {
                effect->y = (int16_t)(effect->goalY - 120);
                effect->step &= 0x7F;
                return WOR_CHANGED;
            }
            effect->y = (int16_t)(effect->y + 4);
            if (effect->y < effect->goalY) return WOR_CHANGED;
            effect->y = effect->goalY;
            effect->step = (uint8_t)((effect->step + 1) | 0x80);
            if (wor_impactVisual(runtime, 7, effect->x, effect->y) == WOR_ERROR) return WOR_ERROR;
            wor_areaDamage(runtime, effect, true, 3, 3, true, effect->damage);
            return WOR_CHANGED;
        case 5:
            effect->used = false;
            return WOR_CHANGED;
        default:
            return wor_error(runtime, WOR_UNIT_COUNT, "unsupported effect phase");
    }
}

static WarOrderResult wor_breakTerrain(WarOriginalRuntime* runtime, WarOriginalTerrain* terrain,
                                       int32_t x, int32_t y, bool linked)
{
    if ((uint32_t)x >= WOR_MAP_WIDTH || (uint32_t)y >= WOR_MAP_WIDTH) return WOR_UNCHANGED;
    const uint32_t tile = (uint32_t)(y * WOR_MAP_WIDTH + x);
    const uint16_t art = (uint16_t)((terrain->visual[tile] & 0x7FF) - 10);
    const uint16_t width = wor_readWord(terrain->wallTable);
    const uint16_t height = wor_readWord(terrain->wallTable + 2);
    if (art >= height - 1) return WOR_UNCHANGED;
    const uint32_t offset = 4 + ((uint32_t)(art + 1) * width + 1) * 2;
    if (offset + 2 > terrain->wallTableLength)
        return wor_error(runtime, WOR_UNIT_COUNT, "truncated terrain transition");
    const uint16_t transition = wor_readWord(terrain->wallTable + offset);
    const uint16_t next = transition & 0xFFF;
    if (next == art + 1) return WOR_UNCHANGED;
    terrain->visual[tile] = (uint16_t)((terrain->visual[tile] & 0xF800) | (next + 9));
    if (!(transition & 0x1000)) terrain->flags[tile] &= (uint16_t)~0x1C;
    if (linked && (transition & 0x2000))
    {
        if (wor_breakTerrain(runtime, terrain, x, y - 1, false) == WOR_ERROR ||
            wor_breakTerrain(runtime, terrain, x, y + 1, false) == WOR_ERROR)
            return WOR_ERROR;
    }
    return WOR_CHANGED;
}

WarOrderResult wor_damageTerrain(WarOriginalRuntime* runtime, WarOriginalTerrain* terrain,
                                int32_t x, int32_t y, uint8_t damage)
{
    if ((uint32_t)x >= WOR_MAP_WIDTH || (uint32_t)y >= WOR_MAP_WIDTH) return WOR_UNCHANGED;
    const uint32_t tile = (uint32_t)(y * WOR_MAP_WIDTH + x);
    if (!(terrain->flags[tile] & 0xC)) return WOR_UNCHANGED;
    if (!terrain->wallTable || terrain->wallTableLength < 4 ||
        wor_readWord(terrain->wallTable) < 2 || wor_readWord(terrain->wallTable + 2) < 1)
        return wor_error(runtime, WOR_UNIT_COUNT, "invalid terrain transition table");
    uint8_t amount = (uint8_t)((damage >> 1) + 1);
    if (amount > 20) amount = 20;
    int32_t remaining = (terrain->visual[tile] >> 11) - amount;
    if (remaining <= 0)
    {
        if (wor_breakTerrain(runtime, terrain, x, y, terrain->linkedBreaks) == WOR_ERROR)
            return WOR_ERROR;
        remaining += 20;
    }
    terrain->visual[tile] = (uint16_t)((terrain->visual[tile] & 0x7FF) | (remaining << 11));
    return WOR_CHANGED;
}

static bool wor_postPlaceholder(const WarOriginalUnit* unit, uint8_t owner)
{
    return (unit->flags & WOR_PLACEHOLDER) && unit->owner == owner &&
           (unit->type < 32 || unit->type > 50);
}

WarOrderResult wor_takePost(WarOriginalRuntime* runtime, uint16_t slot, bool* alerted)
{
    if (slot >= WOR_UNIT_COUNT)
        return wor_error(runtime, slot, "invalid replacement-post unit");
    WarOriginalUnit* unit = &runtime->units[slot];
    const bool wasAlerted = *alerted;
    if (wasAlerted)
    {
        *alerted = false;
        for (uint16_t i = 0; i < WOR_UNIT_COUNT; i++)
            if (wor_postPlaceholder(&runtime->units[i], unit->owner))
            {
                *alerted = true;
                break;
            }
    }
    // The entry value, not the refreshed cache, reverses the odds (0x29B8C).
    const bool nonzero = (runtime->random(runtime) & 3) != 0;
    if (nonzero != wasAlerted) return WOR_UNCHANGED;
    for (uint16_t i = 0; i < WOR_UNIT_COUNT; i++)
    {
        WarOriginalUnit* post = &runtime->units[i];
        if (!wor_postPlaceholder(post, unit->owner) || post->postType != unit->type) continue;
        unit->postX = post->postX;
        unit->postY = post->postY;
        post->flags = WOR_FREE;
        return WOR_CHANGED;
    }
    return WOR_UNCHANGED;
}

// The shared tile/pixel line accumulator (0x1DFFC, 0x1E084).
void wor_lineInit(WarOriginalLine* line, int32_t sx, int32_t sy, int32_t gx, int32_t gy)
{
    int32_t dx = gx - sx, dy = gy - sy;
    line->xPositive = dx >= 0;
    line->yPositive = dy >= 0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    line->xMajor = dx >= dy;
    line->major = line->xMajor ? dx : dy;
    line->minor = line->xMajor ? dy : dx;
    line->err = line->major / 2;
    if (!line->err) line->err = 1;
}

uint8_t wor_lineStep(WarOriginalLine* line, int32_t* dx, int32_t* dy)
{
    static const uint8_t directions[9] = { 7, 0, 1, 6, 0, 2, 5, 4, 3 };
    int32_t a = 0, b = -1;
    line->err -= line->minor;
    if (line->err <= 0) { a = -1; line->err += line->major; }
    if (line->xMajor) { int32_t tmp = a; a = b; b = tmp; }
    if (line->xPositive) a = -a;
    if (line->yPositive) b = -b;
    *dx = a;
    *dy = b;
    return directions[(b + 1) * 3 + a + 1];
}

bool wor_treeAround(const uint8_t trees[WOR_MAP_WIDTH * WOR_MAP_WIDTH],
                    int32_t x, int32_t y, int32_t* tx, int32_t* ty)
{
    static const int8_t dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    for (int32_t size = 3; size < 10; size += 2)
    {
        x--;
        y--;
        for (unsigned side = 0; side < 4; side++)
        {
            for (int32_t k = 0; k < size; k++)
            {
                if (x >= 0 && x < WOR_MAP_WIDTH && y >= 0 && y < WOR_MAP_WIDTH &&
                    trees[y * WOR_MAP_WIDTH + x])
                {
                    *tx = x;
                    *ty = y;
                    return true;
                }
                x += dx[side];
                y += dy[side];
            }
            x -= dx[side];
            y -= dy[side];
        }
    }
    return false;
}

static uint8_t wor_animation(uint8_t order)
{
    if (order <= 3) return order;
    if ((order >= 4 && order <= 8) || order == 10 || order == 14 ||
        (order >= 17 && order <= 27)) return 4;
    if (order == 12) return 3;
    if (order == 15 || order == 28) return 5;
    return 2;
}

static bool wor_word(const WarOriginalRuntime* runtime, uint32_t offset, uint16_t* value)
{
    if (!runtime->scripts || offset >= runtime->scriptLength || runtime->scriptLength - offset < 2)
        return false;
    *value = wor_readWord(runtime->scripts + offset);
    return true;
}

static bool wor_scriptStart(const WarOriginalRuntime* runtime, uint8_t type, uint8_t animation, uint16_t* pc)
{
    uint16_t table;
    return wor_word(runtime, (uint32_t)type * 2, &table) &&
        wor_word(runtime, (uint32_t)(table & ~1u) + (uint32_t)animation * 2, pc) &&
        *pc < runtime->scriptLength;
}

void wor_clearUnits(WarOriginalUnit units[WOR_UNIT_COUNT])
{
    memset(units, 0, sizeof(*units) * WOR_UNIT_COUNT);
    for (unsigned i = 0; i < WOR_UNIT_COUNT; i++)
    {
        units[i].flags = WOR_FREE;
        units[i].pending = units[i].next = WOR_NONE;
    }
}

static WarOrderResult wor_select(WarOriginalRuntime* runtime, uint16_t slot,
                                 uint8_t order, bool randomEntry, bool fromNext, bool replace)
{
    WarOriginalUnit* unit = &runtime->units[slot];
    uint16_t pc;
    if (!wor_scriptStart(runtime, unit->type, wor_animation(order), &pc))
        return wor_error(runtime, slot, "invalid order script");
    if (randomEntry && !runtime->random)
        return wor_error(runtime, slot, "missing simulation random generator");

    unit->current = order;
    if (replace) unit->pending = unit->next = WOR_NONE;
    else if (fromNext) unit->next = WOR_NONE;
    else unit->pending = WOR_NONE;
    if (order == 2 && wor_mobile(unit)) unit->target = 0;
    unit->pc = pc;
    unit->wait = 1;
    if (randomEntry) unit->wait = (uint8_t)((runtime->random(runtime) & 7) + 1);
    unit->flags &= (uint16_t)~(WOR_RANDOM_ENTRY | WOR_SUPPRESS_ACTION);
    if (runtime->selected) runtime->selected(runtime, slot);
    return WOR_CHANGED;
}

// Immediate replacement rejects inactive records, but not order 12 (0x2EF40).
WarOrderResult wor_replace(WarOriginalRuntime* runtime, uint16_t slot, uint8_t order)
{
    if (slot >= WOR_UNIT_COUNT || order >= WOR_ORDER_COUNT)
        return wor_error(runtime, slot, "invalid immediate order");
    if (runtime->units[slot].flags & WOR_INACTIVE) return WOR_UNCHANGED;
    return wor_select(runtime, slot, order, false, false, true);
}

// Animation selection does not change order bytes or reject dying units (0x26450).
WarOrderResult wor_selectAnimation(WarOriginalRuntime* runtime, uint16_t slot, uint8_t animation)
{
    if (slot >= WOR_UNIT_COUNT || animation >= 6)
        return wor_error(runtime, slot, "invalid animation selection");
    WarOriginalUnit* unit = &runtime->units[slot];
    uint16_t pc;
    if (!wor_scriptStart(runtime, unit->type, animation, &pc))
        return wor_error(runtime, slot, "invalid animation script");
    unit->pc = pc;
    unit->wait = 1;
    return WOR_CHANGED;
}

// Record transitions at the death checkpoint; building cleanup and effects
// are the caller's responsibility (0x13F68).
WarOrderResult wor_finishDeath(WarOriginalRuntime* runtime, uint16_t slot)
{
    if (slot >= WOR_UNIT_COUNT)
        return wor_error(runtime, slot, "invalid death record");
    WarOriginalUnit* unit = &runtime->units[slot];
    if (unit->current != 1 || unit->type > 51)
        return wor_error(runtime, slot, "invalid death checkpoint");
    uint8_t animation = 0;
    if (unit->type <= 3 || (unit->type >= 6 && unit->type <= 13))
        animation = (uint8_t)(1 + (unit->type & 1));
    else if (unit->type == 14 || unit->type == 17 || unit->type == 18)
        animation = 2;
    else if (unit->type == 15 || unit->type == 16 || unit->type == 25)
        animation = 1;
    if (animation)
    {
        unit->type = 51;
        unit->owner = 4;
        unit->frame = 0;
        unit->flags |= WOR_DYING;
        return wor_selectAnimation(runtime, slot, animation);
    }
    unit->flags &= (uint16_t)~(WOR_DYING | WOR_REMOVED);
    WarOrderResult result = wor_replace(runtime, slot, 0);
    unit->flags |= WOR_REMOVED;
    return result;
}

WarOrderResult wor_queue(WarOriginalRuntime* runtime, uint16_t slot, uint8_t pending, uint8_t next)
{
    if (slot >= WOR_UNIT_COUNT || pending >= WOR_ORDER_COUNT || next >= WOR_ORDER_COUNT)
        return wor_error(runtime, slot, "invalid queued order");
    WarOriginalUnit* unit = &runtime->units[slot];
    if ((unit->flags & WOR_INACTIVE) || unit->current == 12) return WOR_UNCHANGED;
    unit->pending = pending;
    unit->next = next;
    unit->flags &= (uint16_t)~WOR_RANDOM_ENTRY;
    return WOR_CHANGED;
}

WarOrderResult wor_consume(WarOriginalRuntime* runtime, uint16_t slot, bool allowNext)
{
    if (slot >= WOR_UNIT_COUNT) return wor_error(runtime, slot, "invalid unit slot");
    WarOriginalUnit* unit = &runtime->units[slot];
    if (unit->flags & WOR_INACTIVE) return WOR_UNCHANGED;
    const bool fromNext = unit->pending == WOR_NONE;
    if (fromNext && (!allowNext || unit->next == WOR_NONE)) return WOR_UNCHANGED;
    const uint8_t order = fromNext ? unit->next : unit->pending;
    if (order >= WOR_ORDER_COUNT) return wor_error(runtime, slot, "invalid pending order");
    return wor_select(runtime, slot, order, (unit->flags & WOR_RANDOM_ENTRY) != 0, fromNext, false);
}

static WarOriginalUnit* wor_reference(WarOriginalRuntime* runtime, uint16_t reference)
{
    return reference && reference <= WOR_UNIT_COUNT ? &runtime->units[reference - 1] : NULL;
}

static uint8_t wor_sightSize(uint8_t type)
{
    if (type >= 32 && type < 50) return 8;
    if (type == 4 || type == 5) return 11;
    if ((type >= 8 && type <= 10) || type == 31) return 9;
    if (type <= 3 || type == 12 || type == 21 ||
        (type >= 24 && type <= 26) || type == 50) return 5;
    return 7;
}

// Score the human's top-left unit tiles, including hidden workers (0x29734).
WarOrderResult wor_attackZone(WarOriginalRuntime* runtime, int16_t* x, int16_t* y)
{
    uint16_t scores[16] = {0};
    for (uint16_t slot = 0; slot < WOR_UNIT_COUNT; slot++)
    {
        const WarOriginalUnit* unit = &runtime->units[slot];
        if (unit->owner != 0 || (unit->flags & WOR_INACTIVE)) continue;
        if (unit->tileX < 0 || unit->tileX >= WOR_MAP_WIDTH ||
            unit->tileY < 0 || unit->tileY >= WOR_MAP_WIDTH)
            return wor_error(runtime, slot, "invalid attack-zone tile");
        unsigned zone = (unsigned)(unit->tileY / 16) * 4 + (unsigned)(unit->tileX / 16);
        scores[zone] += unit->type >= 32 && unit->type <= 50 ? 4 : 1;
    }
    unsigned best = 0;
    for (unsigned zone = 1; zone < 16; zone++)
        if (scores[zone] > scores[best]) best = zone;
    *x = (int16_t)((best % 4) * 16 + 8);
    *y = (int16_t)((best / 4) * 16 + 8);
    return WOR_CHANGED;
}

// Only fully unrevealed tiles hide a unit. Stencil unions never hide a
// revealed tile, so simulation needs two bits, not the fog artwork (0x1EA90).
static void wor_reveal(WarOriginalVision* vision, int x, int y, uint8_t size, uint8_t side)
{
    const int corner = size == 5 ? 1 : 2;
    const int left = x - size / 2, top = y - size / 2;
    for (int row = 0; row < size; row++)
    {
        int edge = row < size - 1 - row ? row : size - 1 - row;
        int inset = edge < corner ? corner - edge : 0;
        int ty = top + row;
        if (ty < 0 || ty >= WOR_MAP_WIDTH) continue;
        for (int col = inset; col < size - inset; col++)
        {
            int tx = left + col;
            if (tx >= 0 && tx < WOR_MAP_WIDTH)
                vision->tiles[ty * WOR_MAP_WIDTH + tx] |= (uint8_t)(1u << side);
        }
    }
}

static bool wor_revealsCombat(WarOriginalRuntime* runtime, const WarOriginalUnit* unit)
{
    if (unit->current == 26 || unit->current == 27) return true;
    if (unit->current < 4 || unit->current > 8 || unit->current == 7) return false;
    const WarOriginalUnit* target = wor_reference(runtime, unit->target);
    return target && wor_areEnemies(unit->owner, target->owner);
}

// Exploration is a checkpoint side effect, including after a tile step (0x2EB3C).
WarOrderResult wor_explore(WarOriginalRuntime* runtime, WarOriginalVision* vision, uint16_t slot,
                          uint8_t width, uint8_t height, bool humanController, bool networkGame)
{
    if (slot >= WOR_UNIT_COUNT || !vision || !width || !height)
        return wor_error(runtime, slot, "invalid exploration record");
    WarOriginalUnit* unit = &runtime->units[slot];
    if (unit->flags & WOR_INACTIVE) return WOR_UNCHANGED;
    const int x = unit->tileX + width / 2, y = unit->tileY + height / 2;
    if (unit->current == 3)
    {
        unit->flags &= (uint16_t)~0x120;
        if (humanController && unit->owner <= 1)
            wor_reveal(vision, x, y, 5, unit->owner);
        return WOR_CHANGED;
    }
    const bool combat = wor_revealsCombat(runtime, unit);
    if (!humanController)
    {
        if (!(unit->flags & 0x20) && combat)
        {
            unit->flags |= 0x20;
            wor_reveal(vision, x, y, 5, 0);
        }
        return WOR_CHANGED;
    }
    const uint8_t side = unit->owner == 0 ? 0 : 1;
    const uint16_t ownFlag = side ? 0x100 : 0x20, otherFlag = side ? 0x20 : 0x100;
    if (!(unit->flags & ownFlag))
    {
        unit->flags |= ownFlag;
        wor_reveal(vision, x, y, wor_sightSize(unit->type), side);
    }
    if (networkGame && combat && !(unit->flags & otherFlag))
    {
        unit->flags |= otherFlag;
        wor_reveal(vision, x, y, 5, (uint8_t)(side ^ 1));
    }
    return WOR_CHANGED;
}

// Snapshot visibility before the game tick; dying records are still examined (0x2D740).
WarOrderResult wor_refreshVisibility(WarOriginalRuntime* runtime, const WarOriginalVision* vision,
                                    uint16_t slot, uint8_t width, uint8_t height, bool revealAll)
{
    if (slot >= WOR_UNIT_COUNT || !vision || !width || !height)
        return wor_error(runtime, slot, "invalid visibility record");
    WarOriginalUnit* unit = &runtime->units[slot];
    unit->visibility |= 0x300;
    if (unit->flags & (WOR_FREE | WOR_REMOVED | WOR_INSIDE | WOR_PLACEHOLDER))
        return WOR_UNCHANGED;
    if (revealAll) unit->visibility &= (uint16_t)~0x300;
    else
    {
        for (int y = unit->tileY; y < unit->tileY + height; y++)
            for (int x = unit->tileX; x < unit->tileX + width; x++)
            {
                if (x < 0 || x >= WOR_MAP_WIDTH || y < 0 || y >= WOR_MAP_WIDTH) continue;
                uint8_t revealed = vision->tiles[y * WOR_MAP_WIDTH + x];
                if (revealed & 1) unit->visibility &= (uint16_t)~0x100;
                if (revealed & 2) unit->visibility &= (uint16_t)~0x200;
            }
    }
    return WOR_CHANGED;
}

// Command prologue (0x2E1F0); the caller then constructs orders and resolves the goal.
WarOrderResult wor_beginCommand(WarOriginalRuntime* runtime, uint16_t slot,
                                uint16_t target, int16_t x, int16_t y)
{
    if (slot >= WOR_UNIT_COUNT || target > WOR_UNIT_COUNT)
        return wor_error(runtime, slot, "invalid command reference");
    WarOriginalUnit* unit = &runtime->units[slot];
    if (unit->current <= 1 || unit->current == 12) return WOR_UNCHANGED;
    unit->flags = (uint16_t)((unit->flags | WOR_SUPPRESS_ACTION) & ~WOR_RANDOM_ENTRY);
    unit->sound = 0;
    unit->target = target;
    WarOriginalUnit* other = wor_reference(runtime, target);
    unit->goalX = other ? other->tileX : x;
    unit->goalY = other ? other->tileY : y;
    return WOR_CHANGED;
}

static bool wor_targetActive(const WarOriginalUnit* unit)
{
    return unit && !(unit->flags & (WOR_INACTIVE | WOR_INSIDE)) && !unit->invisibility;
}

// The checkpoint only checks mana; the action pays and refreshes the timer
// before attempting the presentation effect (0x27890).
WarOrderResult wor_invisibility(WarOriginalRuntime* runtime, uint16_t slot,
                               uint8_t* mana, bool action)
{
    if (slot >= WOR_UNIT_COUNT || !mana)
        return wor_error(runtime, slot, "invalid invisibility caster");
    if (*mana < 125) return WOR_UNCHANGED;
    if (action)
    {
        WarOriginalUnit* target = wor_reference(runtime, runtime->units[slot].target);
        if (!target) return wor_error(runtime, slot, "missing invisibility recipient");
        *mana -= 125;
        target->invisibility = 3000;
    }
    return WOR_CHANGED;
}

// Healing restores at most forty HP for three mana each; on a daemon it
// subtracts HP instead, without entering the damage/death path (0x27760).
WarOrderResult wor_healing(WarOriginalRuntime* runtime, uint16_t slot,
                          uint8_t* mana, uint16_t* health, uint16_t baseHealth, bool action)
{
    if (slot >= WOR_UNIT_COUNT || !mana || !health)
        return wor_error(runtime, slot, "invalid healing caster");
    WarOriginalUnit* target = wor_reference(runtime, runtime->units[slot].target);
    if (!target) return wor_error(runtime, slot, "missing healing recipient");
    if (target->type >= 32 && target->type <= 50) return WOR_UNCHANGED;
    if (!action) return *mana ? WOR_CHANGED : WOR_UNCHANGED;
    uint16_t amount = target->type == 28 ? *health : (uint16_t)(baseHealth - *health);
    if (amount > 40) amount = 40;
    if (amount * 3 > *mana) amount = *mana / 3;
    *health = target->type == 28 ? (uint16_t)(*health - amount) : (uint16_t)(*health + amount);
    *mana = (uint8_t)(*mana - amount * 3);
    return WOR_CHANGED;
}

// Low-health recipients become one HP, otherwise floor-half, with a
// thousand-tick protection timer refreshed on every cast (0x278C4).
WarOrderResult wor_unholyArmour(WarOriginalRuntime* runtime, uint16_t slot,
                               uint8_t* mana, uint16_t* health, bool action)
{
    if (slot >= WOR_UNIT_COUNT || !mana || !health)
        return wor_error(runtime, slot, "invalid armour caster");
    if (*mana < 125) return WOR_UNCHANGED;
    if (action)
    {
        WarOriginalUnit* target = wor_reference(runtime, runtime->units[slot].target);
        if (!target) return wor_error(runtime, slot, "missing armour recipient");
        *mana -= 125;
        *health = *health <= 30 ? 1 : *health / 2;
        target->invulnerability = 1000;
    }
    return WOR_CHANGED;
}

static bool wor_corpseNear(const WarOriginalUnit* caster, const WarOriginalUnit* corpse, int radiusSquared)
{
    if (corpse->type != 51 || !(corpse->flags & WOR_DYING) ||
        (corpse->flags & (WOR_FREE | WOR_REMOVED | WOR_INSIDE | WOR_PLACEHOLDER))) return false;
    const int dx = caster->tileX - corpse->tileX, dy = caster->tileY - corpse->tileY;
    return dx * dx + dy * dy <= radiusSquared;
}

// The first retained corpse strictly less than ten tiles away (0x2A4BC).
uint16_t wor_findCorpse(WarOriginalRuntime* runtime, uint16_t slot)
{
    if (slot >= WOR_UNIT_COUNT)
    {
        wor_error(runtime, slot, "invalid corpse-search caster");
        return 0;
    }
    for (uint16_t corpse = 0; corpse < WOR_UNIT_COUNT; corpse++)
        if (wor_corpseNear(&runtime->units[slot], &runtime->units[corpse], 99))
            return corpse + 1;
    return 0;
}

// Search around the caster, charge successful creations and hide each used
// corpse without releasing its script record (0x2799C).
WarOrderResult wor_resurrect(WarOriginalRuntime* runtime, uint16_t slot,
                            uint8_t* mana, bool action)
{
    if (slot >= WOR_UNIT_COUNT || !mana)
        return wor_error(runtime, slot, "invalid resurrection caster");
    if (*mana < 70) return WOR_UNCHANGED;
    if (action)
    {
        if (!runtime->resurrect) return wor_error(runtime, slot, "missing resurrection callback");
        for (uint16_t corpse = 0; corpse < WOR_UNIT_COUNT && *mana >= 70; corpse++)
            if (wor_corpseNear(&runtime->units[slot], &runtime->units[corpse], 36) &&
                runtime->resurrect(runtime, slot, corpse))
            {
                *mana -= 70;
                runtime->units[corpse].flags |= WOR_INSIDE;
            }
    }
    return WOR_CHANGED;
}

// Four attempts, not four successes. Failed placement/allocation costs no
// mana and does not end the loop (0x27AB0).
WarOrderResult wor_summon(WarOriginalRuntime* runtime, uint16_t slot,
                         uint8_t* mana, bool action)
{
    if (slot >= WOR_UNIT_COUNT || !mana)
        return wor_error(runtime, slot, "invalid summoning caster");
    const uint8_t order = runtime->units[slot].current;
    if (order < 22 || order > 25)
        return wor_error(runtime, slot, "invalid summoning order");
    const uint8_t cost = order <= 23 ? 255 : 60;
    const uint8_t type = order == 22 ? 31 : order == 23 ? 28 : order == 24 ? 21 : 24;
    if (*mana < cost) return WOR_UNCHANGED;
    if (action)
    {
        if (!runtime->summon) return wor_error(runtime, slot, "missing summoning callback");
        for (unsigned attempt = 0; attempt < 4 && *mana >= cost; attempt++)
            if (runtime->summon(runtime, slot, type)) *mana -= cost;
    }
    return WOR_CHANGED;
}

void wor_tickSpellTimers(WarOriginalRuntime* runtime)
{
    for (uint16_t slot = 0; slot < WOR_UNIT_COUNT; slot++)
    {
        WarOriginalUnit* unit = &runtime->units[slot];
        if (unit->flags & WOR_INACTIVE) continue;
        if (unit->invisibility) unit->invisibility--;
        if (unit->invulnerability) unit->invulnerability--;
    }
}

// The checkpoint tests affordability; the action subtracts a byte before allocation (0x110A4, 0x106EC).
WarOrderResult wor_shotMana(WarOriginalRuntime* runtime, uint16_t slot,
                           uint8_t controller, uint8_t* mana, bool action)
{
    if (slot >= WOR_UNIT_COUNT || !mana)
        return wor_error(runtime, slot, "invalid shot caster");
    const uint8_t type = runtime->units[slot].type;
    if (type < 10 || type > 14) return WOR_CHANGED;
    const uint8_t cost = controller == 0 ? 5 : 1;
    if (action) *mana = (uint8_t)(*mana - cost);
    else if (*mana < cost) return WOR_UNCHANGED;
    return WOR_CHANGED;
}

// Full mana freezes the byte counter; only controller 1 uses sixty ticks (0x2D3D0).
WarOrderResult wor_regenerateMana(WarOriginalRuntime* runtime, uint16_t slot,
                                 uint8_t controller, uint8_t* mana)
{
    if (slot >= WOR_UNIT_COUNT || !mana)
        return wor_error(runtime, slot, "invalid mana record");
    WarOriginalUnit* unit = &runtime->units[slot];
    if ((unit->flags & WOR_INACTIVE) || unit->type < 10 || unit->type > 14 || *mana == 255)
        return WOR_UNCHANGED;
    unit->statusTicks--;
    if (unit->statusTicks) return WOR_UNCHANGED;
    unit->statusTicks = controller == 1 ? 60 : 40;
    (*mana)++;
    return WOR_CHANGED;
}

// Level placements do not decay; zero HP is killed only on the next due update (0x2D3D0).
WarOrderResult wor_decay(WarOriginalRuntime* runtime, uint16_t slot,
                        uint8_t* mana, uint16_t* health, bool* dead)
{
    if (slot >= WOR_UNIT_COUNT || !mana || !health || !dead)
        return wor_error(runtime, slot, "invalid decay record");
    *dead = false;
    WarOriginalUnit* unit = &runtime->units[slot];
    if ((unit->flags & (WOR_INACTIVE | 0x400)) ||
        (unit->type != 21 && unit->type != 24 && unit->type != 26 &&
         unit->type != 28 && unit->type != 31)) return WOR_UNCHANGED;
    unit->statusTicks--;
    if (unit->statusTicks) return WOR_UNCHANGED;
    if (*mana) (*mana)--;
    else if (*health < 5) *dead = true;
    else *health -= 5;
    unit->statusTicks = 45;
    return WOR_CHANGED;
}

bool wor_validateTarget(WarOriginalRuntime* runtime, uint16_t slot)
{
    if (slot >= WOR_UNIT_COUNT)
    {
        wor_error(runtime, slot, "invalid unit slot");
        return false;
    }
    WarOriginalUnit* unit = &runtime->units[slot];
    if (!unit->target) return true;
    if (!wor_targetActive(wor_reference(runtime, unit->target)))
    {
        if (unit->attacker == unit->target) unit->attacker = 0;
        unit->target = 0;
        return false;
    }
    if (unit->attacker && !wor_targetActive(wor_reference(runtime, unit->attacker)))
        unit->attacker = 0;
    return true;
}

void wor_noteAttacker(WarOriginalRuntime* runtime, uint16_t victim, uint16_t attacker)
{
    if (victim >= WOR_UNIT_COUNT || attacker >= WOR_UNIT_COUNT)
    {
        wor_error(runtime, victim, "invalid attacker slot");
        return;
    }
    WarOriginalUnit* unit = &runtime->units[victim];
    const WarOriginalUnit* source = &runtime->units[attacker];
    if ((unit->flags & WOR_INACTIVE) || !wor_areEnemies(unit->owner, source->owner)) return;
    const WarOriginalUnit* previous = wor_reference(runtime, unit->attacker);
    if (!previous || (previous->flags & WOR_INACTIVE)) unit->attacker = (uint16_t)(attacker + 1);
}

// A checkpoint is WOR_CHANGED; a wait/stop/countdown is WOR_UNCHANGED (0x10473).
WarOrderResult wor_scriptTick(WarOriginalRuntime* runtime, uint16_t slot, uint8_t speed, bool slowed)
{
    if (slot >= WOR_UNIT_COUNT) return wor_error(runtime, slot, "invalid unit slot");
    WarOriginalUnit* unit = &runtime->units[slot];
    if (!wscr_countdown(&unit->wait)) return WOR_UNCHANGED;
    uint32_t pc = unit->pc;
    for (unsigned n = 0; n < 64; n++)
    {
        WarScriptInstruction instruction;
        if (!wscr_decode(runtime->scripts, runtime->scriptLength, pc, speed, slowed, &instruction))
            return wor_error(runtime, slot, "invalid script instruction");
        pc = instruction.next;
        switch (instruction.opcode)
        {
            case 0: case 1: case 2: case 7: case 10:
                unit->pc = (uint16_t)pc;
                unit->wait = instruction.wait;
                return instruction.opcode == 0 ? WOR_CHANGED : WOR_UNCHANGED;
            case 3: break;
            case 4: unit->frame = instruction.frame; break;
            case 5: case 6:
            {
                static const int8_t dx[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
                static const int8_t dy[] = { -1, -1, 0, 1, 1, 1, 0, -1 };
                const int8_t pixels = (int8_t)instruction.operand;
                unit->pixelX = (int16_t)(unit->pixelX + dx[unit->direction & 7] * pixels);
                unit->pixelY = (int16_t)(unit->pixelY + dy[unit->direction & 7] * pixels);
                if (instruction.opcode == 6) unit->frame = instruction.frame;
                break;
            }
            case 8:
            {
                const uint8_t sound = unit->sound;
                unit->sound = 0;
                if (runtime->sound) runtime->sound(runtime, slot, sound);
                break;
            }
            case 9:
                if (unit->flags & WOR_SUPPRESS_ACTION) break;
                unit->invisibility = 0;
                if (unit->current == 10 || unit->current == 14) break;
                if (!((unit->current >= 4 && unit->current <= 8) ||
                      (unit->current >= 17 && unit->current <= 27)) || !runtime->action)
                    return wor_error(runtime, slot, "missing order action");
                if (runtime->action(runtime, slot) == WOR_ERROR) return WOR_ERROR;
                break;
        }
    }
    return wor_error(runtime, slot, "script did not yield");
}

// One record's complete dispatch protocol, not a war1 state-stack update (0x2ECFC).
WarOrderResult wor_unitTick(WarOriginalRuntime* runtime, uint16_t slot, uint8_t speed, bool slowed)
{
    if (slot >= WOR_UNIT_COUNT) return wor_error(runtime, slot, "invalid unit slot");
    WarOriginalUnit* unit = &runtime->units[slot];
    if (unit->flags & WOR_FREE) return WOR_UNCHANGED;
    if (unit->flags & WOR_PLACEHOLDER)
    {
        if (unit->placeholderTicks) unit->placeholderTicks--;
        return WOR_UNCHANGED;
    }
    if (unit->timer) unit->timer--;
    if (unit->flags & WOR_HELD)
    {
        if (!(unit->flags & WOR_HOLD_REQUEST)) return WOR_UNCHANGED;
        unit->flags &= (uint16_t)~(WOR_HELD | WOR_HOLD_REQUEST);
    }
    else
    {
        WarOrderResult result = wor_scriptTick(runtime, slot, speed, slowed);
        if (result != WOR_CHANGED) return result;
        if (runtime->visibility) runtime->visibility(runtime, slot);
    }
    if (unit->flags & WOR_HOLD_REQUEST)
    {
        unit->flags = (uint16_t)((unit->flags & ~WOR_HOLD_REQUEST) | WOR_HELD);
        return WOR_UNCHANGED;
    }
    WarOrderResult result = wor_consume(runtime, slot, false);
    if (result != WOR_UNCHANGED) return result;
    if (unit->current >= WOR_ORDER_COUNT || !runtime->order)
        return wor_error(runtime, slot, "missing checkpoint order handler");
    result = runtime->order(runtime, slot);
    if (result != WOR_CHANGED) return result;
    return wor_consume(runtime, slot, false);
}
