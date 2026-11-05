// Independent unit/order execution model for WarCraft 1.20.
// SPDX-License-Identifier: Zlib
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum
{
    WOR_UNIT_COUNT = 200,
    WOR_EFFECT_COUNT = 100,
    WOR_MAP_WIDTH = 64,
    WOR_ORDER_COUNT = 32,
    WOR_NONE = 31,
    WOR_FREE = 0x0001,
    WOR_DYING = 0x0002,
    WOR_REMOVED = 0x0004,
    WOR_INSIDE = 0x0008,
    WOR_PLACEHOLDER = 0x0200,
    WOR_HOLD_REQUEST = 0x0800,
    WOR_HELD = 0x1000,
    WOR_SUPPRESS_ACTION = 0x4000,
    WOR_RANDOM_ENTRY = 0x8000,
    WOR_INACTIVE = WOR_FREE | WOR_DYING | WOR_REMOVED | WOR_PLACEHOLDER
};

typedef enum
{
    WOR_ERROR = -1,
    WOR_UNCHANGED = 0,
    WOR_CHANGED = 1
} WarOrderResult;

typedef struct
{
    uint16_t flags;
    uint16_t pc;
    uint16_t timer;
    uint16_t placeholderTicks;
    uint16_t invisibility;
    uint16_t invulnerability;
    uint16_t visibility;
    uint16_t target, attacker; // slot + 1; zero means no reference
    int16_t tileX, tileY, pixelX, pixelY;
    int16_t goalX, goalY;
    int16_t postX, postY;
    uint16_t postType;
    uint8_t type, owner;
    uint8_t current, pending, next;
    uint8_t wait, frame, direction, sound, workFlags, statusTicks;
} WarOriginalUnit;

typedef struct
{
    uint8_t tiles[WOR_MAP_WIDTH * WOR_MAP_WIDTH];
} WarOriginalVision;

typedef struct
{
    int32_t err, major, minor;
    bool xMajor, xPositive, yPositive;
} WarOriginalLine;

typedef struct
{
    bool used;
    uint8_t type, state, wait, damage, step, frame, direction, sound;
    uint16_t pc, pulses, source;
    int16_t run, x, y;
    WarOriginalLine line;
    uint32_t shooter, target, visual;
} WarOriginalEffect;

typedef struct
{
    uint16_t* flags;
    uint16_t* visual;
    const uint8_t* wallTable;
    uint32_t wallTableLength;
    bool linkedBreaks;
} WarOriginalTerrain;

void wor_lineInit(WarOriginalLine* line, int32_t sx, int32_t sy, int32_t gx, int32_t gy);
uint8_t wor_lineStep(WarOriginalLine* line, int32_t* dx, int32_t* dy);
uint8_t wor_facing(int16_t dx, int16_t dy);
bool wor_areEnemies(uint8_t first, uint8_t second);
bool wor_treeAround(const uint8_t trees[WOR_MAP_WIDTH * WOR_MAP_WIDTH],
                    int32_t x, int32_t y, int32_t* tx, int32_t* ty);

typedef struct WarOriginalRuntime WarOriginalRuntime;
struct WarOriginalRuntime
{
    WarOriginalUnit* units; // exactly WOR_UNIT_COUNT stable records
    const uint8_t* scripts;
    uint32_t scriptLength;
    void* context;
    uint16_t (*random)(WarOriginalRuntime*);
    WarOrderResult (*order)(WarOriginalRuntime*, uint16_t slot);
    WarOrderResult (*action)(WarOriginalRuntime*, uint16_t slot);
    void (*visibility)(WarOriginalRuntime*, uint16_t slot);
    void (*sound)(WarOriginalRuntime*, uint16_t slot, uint8_t sound);
    void (*selected)(WarOriginalRuntime*, uint16_t slot);
    bool (*blocked)(WarOriginalRuntime*, int16_t x, int16_t y);
    bool (*summon)(WarOriginalRuntime*, uint16_t caster, uint8_t type);
    bool (*resurrect)(WarOriginalRuntime*, uint16_t caster, uint16_t corpse);
    uint16_t (*occupant)(WarOriginalRuntime*, int16_t x, int16_t y);
    WarOriginalEffect* effects;
    int8_t windX, windY;
    void (*damage)(WarOriginalRuntime*, uint16_t source, uint16_t target, uint8_t damage);
    void (*effectCreated)(WarOriginalRuntime*, WarOriginalEffect*);
    void (*effectSound)(WarOriginalRuntime*, WarOriginalEffect*, uint8_t sound);
    void (*error)(WarOriginalRuntime*, uint16_t slot, const char* message);
};

void wor_clearUnits(WarOriginalUnit units[WOR_UNIT_COUNT]);
WarOrderResult wor_replace(WarOriginalRuntime* runtime, uint16_t slot, uint8_t order);
WarOrderResult wor_selectAnimation(WarOriginalRuntime* runtime, uint16_t slot, uint8_t animation);
WarOrderResult wor_finishDeath(WarOriginalRuntime* runtime, uint16_t slot);
WarOrderResult wor_queue(WarOriginalRuntime* runtime, uint16_t slot, uint8_t pending, uint8_t next);
WarOrderResult wor_consume(WarOriginalRuntime* runtime, uint16_t slot, bool allowNext);
WarOrderResult wor_beginCommand(WarOriginalRuntime* runtime, uint16_t slot,
                                uint16_t target, int16_t x, int16_t y);
bool wor_validateTarget(WarOriginalRuntime* runtime, uint16_t slot);
void wor_noteAttacker(WarOriginalRuntime* runtime, uint16_t victim, uint16_t attacker);
WarOrderResult wor_explore(WarOriginalRuntime* runtime, WarOriginalVision* vision, uint16_t slot,
                          uint8_t width, uint8_t height, bool humanController, bool networkGame);
WarOrderResult wor_refreshVisibility(WarOriginalRuntime* runtime, const WarOriginalVision* vision,
                                    uint16_t slot, uint8_t width, uint8_t height, bool revealAll);
WarOrderResult wor_attackZone(WarOriginalRuntime* runtime, int16_t* x, int16_t* y);
WarOrderResult wor_takePost(WarOriginalRuntime* runtime, uint16_t slot, bool* alerted);
WarOrderResult wor_damageTerrain(WarOriginalRuntime* runtime, WarOriginalTerrain* terrain,
                                int32_t x, int32_t y, uint8_t damage);
WarOrderResult wor_invisibility(WarOriginalRuntime* runtime, uint16_t slot,
                               uint8_t* mana, bool action);
WarOrderResult wor_healing(WarOriginalRuntime* runtime, uint16_t slot,
                          uint8_t* mana, uint16_t* health, uint16_t baseHealth, bool action);
WarOrderResult wor_unholyArmour(WarOriginalRuntime* runtime, uint16_t slot,
                               uint8_t* mana, uint16_t* health, bool action);
WarOrderResult wor_summon(WarOriginalRuntime* runtime, uint16_t slot,
                         uint8_t* mana, bool action);
uint16_t wor_findCorpse(WarOriginalRuntime* runtime, uint16_t slot);
WarOrderResult wor_resurrect(WarOriginalRuntime* runtime, uint16_t slot,
                            uint8_t* mana, bool action);
WarOrderResult wor_areaTarget(WarOriginalRuntime* runtime, uint16_t slot,
                              int16_t targetX, int16_t targetY, int16_t* x, int16_t* y);
void wor_tickWind(WarOriginalRuntime* runtime, int8_t* x, int8_t* y);
WarOrderResult wor_poison(WarOriginalRuntime* runtime, uint16_t slot, uint8_t* mana, bool action);
WarOrderResult wor_poisonTick(WarOriginalRuntime* runtime, WarOriginalEffect* effect);
WarOrderResult wor_shotMana(WarOriginalRuntime* runtime, uint16_t slot,
                           uint8_t controller, uint8_t* mana, bool action);
void wor_tickSpellTimers(WarOriginalRuntime* runtime);
WarOrderResult wor_regenerateMana(WarOriginalRuntime* runtime, uint16_t slot,
                                 uint8_t controller, uint8_t* mana);
WarOrderResult wor_decay(WarOriginalRuntime* runtime, uint16_t slot,
                        uint8_t* mana, uint16_t* health, bool* dead);
WarOrderResult wor_checkCatapult(WarOriginalRuntime* runtime, uint16_t slot,
                                uint8_t controller, uint8_t minimumRange,
                                uint8_t targetWidth, uint8_t targetHeight);
WarOrderResult wor_findExit(WarOriginalRuntime* runtime, int16_t* x, int16_t* y,
                           uint8_t width, uint8_t height, int16_t towardX, int16_t towardY);
WarOrderResult wor_scriptTick(WarOriginalRuntime* runtime, uint16_t slot, uint8_t speed, bool slowed);
WarOrderResult wor_unitTick(WarOriginalRuntime* runtime, uint16_t slot, uint8_t speed, bool slowed);
