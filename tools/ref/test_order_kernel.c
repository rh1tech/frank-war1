// Direct conformance against the local reference's existing lifted object.
// No original executable, lifted source or game resource is stored here.
// SPDX-License-Identifier: Zlib
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "x86.h"
#include "../../port/war_orders_frank.h"

typedef struct { uint32_t addr; void (*fn)(Cpu*); } LiftedEntry;
extern const LiftedEntry lifted_table[];
extern const unsigned lifted_table_len;

enum { MEMORY_SIZE = 4 * 1024 * 1024, SCRIPTS = 0x100000, UNITS = 0x200000,
       FOG = 0x210000, LINE = 0x220000, TERRAIN = 0x230000, TREE_ATTRIBUTES = 0x232000,
       VISUAL = 0x234000, WALL_TABLE = 0x236000, OCCUPANCY = 0x240000, STACK = 0x300000 };
static Cpu cpu;
static WarOriginalUnit units[WOR_UNIT_COUNT];
static uint8_t scripts[512];
static uint32_t seed, cases, scriptTicks;
static uint32_t routine;
static uint8_t lastCorpseDirection;

static void dispatch(Cpu* machine, uint32_t address, uint32_t at)
{
    (void)at;
    unsigned begin = 0, end = lifted_table_len;
    while (begin < end)
    {
        unsigned middle = begin + (end - begin) / 2;
        if (lifted_table[middle].addr < address) begin = middle + 1;
        else end = middle;
    }
    if (begin == lifted_table_len || lifted_table[begin].addr != address)
        x86_stop(machine, STOP_TRAP, address);
    lifted_table[begin].fn(machine);
}

static const Hooks hooks = { .call = dispatch };

static uint16_t randomValue(WarOriginalRuntime* runtime)
{
    (void)runtime;
    seed = seed * 0x15A4E35u + 1;
    return (uint16_t)((seed >> 16) & 0x7FFF);
}

static void kernelError(WarOriginalRuntime* runtime, uint16_t slot, const char* message)
{
    (void)runtime;
    fprintf(stderr, "kernel error in case %u, slot %u: %s\n", cases, slot, message);
    exit(2);
}

static WarOriginalRuntime runtime = {
    .units = units, .scripts = scripts, .scriptLength = sizeof(scripts),
    .random = randomValue, .error = kernelError
};

static uint32_t reference(uint16_t id)
{
    return id ? UNITS + (uint32_t)(id - 1) * 0x5C : 0;
}

static void exportUnit(unsigned slot)
{
    const WarOriginalUnit* unit = &units[slot];
    const uint32_t base = UNITS + slot * 0x5C;
#define BYTE(field, offset) wr8(&cpu, base + offset, unit->field)
#define WORD(field, offset) wr16(&cpu, base + offset, (uint16_t)unit->field)
    WORD(pixelX, 0); WORD(pixelY, 2); WORD(pc, 4); BYTE(wait, 6); BYTE(frame, 7);
    BYTE(direction, 8); BYTE(sound, 9); WORD(tileX, 0xA); WORD(tileY, 0xC);
    WORD(visibility, 0xE);
    WORD(flags, 0x10); WORD(timer, 0x18); BYTE(type, 0x1B); BYTE(owner, 0x1E);
    BYTE(current, 0x1F); BYTE(pending, 0x20); BYTE(next, 0x21);
    WORD(invisibility, 0x36); WORD(invulnerability, 0x38);
    BYTE(statusTicks, 0x48); BYTE(workFlags, 0x49); WORD(placeholderTicks, 0x4A);
    WORD(goalX, 0x4E); WORD(goalY, 0x50);
    WORD(postX, 0x3E); WORD(postY, 0x40); WORD(postType, 0x46);
#undef BYTE
#undef WORD
    wr32(&cpu, base + 0x3A, reference(unit->attacker));
    wr32(&cpu, base + 0x52, reference(unit->target));
}

static void equal(const char* field, uint32_t expected, uint32_t actual)
{
    if (expected == actual) return;
    fprintf(stderr, "case %u routine 0x%x field %s: original %u, kernel %u\n",
            cases, routine, field, expected, actual);
    exit(1);
}

static void compareUnit(unsigned slot)
{
    const WarOriginalUnit* unit = &units[slot];
    const uint32_t base = UNITS + slot * 0x5C;
#define BYTE(field, offset) equal(#field, rd8(&cpu, base + offset), unit->field)
#define WORD(field, offset) equal(#field, rd16(&cpu, base + offset), (uint16_t)unit->field)
    WORD(pixelX, 0); WORD(pixelY, 2); WORD(pc, 4); BYTE(wait, 6); BYTE(frame, 7);
    BYTE(direction, 8); BYTE(sound, 9); WORD(tileX, 0xA); WORD(tileY, 0xC);
    WORD(visibility, 0xE);
    WORD(flags, 0x10); WORD(timer, 0x18); BYTE(type, 0x1B); BYTE(owner, 0x1E);
    BYTE(current, 0x1F); BYTE(pending, 0x20); BYTE(next, 0x21);
    WORD(invisibility, 0x36); WORD(invulnerability, 0x38);
    BYTE(statusTicks, 0x48); BYTE(workFlags, 0x49); WORD(placeholderTicks, 0x4A);
    WORD(goalX, 0x4E); WORD(goalY, 0x50);
    WORD(postX, 0x3E); WORD(postY, 0x40); WORD(postType, 0x46);
#undef BYTE
#undef WORD
    equal("attacker", rd32(&cpu, base + 0x3A), reference(unit->attacker));
    equal("target", rd32(&cpu, base + 0x52), reference(unit->target));
    equal("random state", rd32(&cpu, 0x50D7C), seed);
}

static void invoke4(uint32_t address, uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t ecx)
{
    routine = address;
    memset(cpu.r, 0, sizeof(cpu.r));
    memset(&cpu.f, 0, sizeof(cpu.f));
    cpu.r[EAX] = eax;
    cpu.r[EDX] = edx;
    cpu.r[EBX] = ebx;
    cpu.r[ECX] = ecx;
    cpu.r[ESP] = STACK;
    cpu.budget = 100000;
    wr32(&cpu, STACK, 0xFFFFFFF0);
    const int stopped = setjmp(cpu.stop);
    if (stopped)
    {
        fprintf(stderr, "reference stopped: case %u routine 0x%x reason %d address 0x%x\n",
                cases, address, stopped, cpu.fault_addr);
        exit(2);
    }
    dispatch(&cpu, address, 0);
    equal("return address", 0xFFFFFFF0, cpu.ret_to);
}

static void invoke(uint32_t address, uint32_t eax, uint32_t edx, uint32_t ebx)
{
    invoke4(address, eax, edx, ebx, 0);
}

static uint32_t mix(uint32_t* value)
{
    *value = *value * 1664525u + 1013904223u;
    return *value ^ (*value >> 16);
}

static void prepare(uint32_t* input)
{
    static const uint16_t flags[] = {
        0, 0x80, WOR_FREE, WOR_DYING, WOR_REMOVED, WOR_INSIDE, WOR_PLACEHOLDER,
        WOR_HELD, WOR_HOLD_REQUEST, WOR_RANDOM_ENTRY, WOR_SUPPRESS_ACTION, 0xC080
    };
    for (unsigned slot = 0; slot < 3; slot++)
    {
        WarOriginalUnit* unit = &units[slot];
        *unit = (WarOriginalUnit){
            .flags = flags[mix(input) % (sizeof(flags) / sizeof(*flags))],
            .type = (uint8_t)(mix(input) % 51), .owner = (uint8_t)(mix(input) % 5),
            .current = (uint8_t)(mix(input) % 32), .pending = (uint8_t)(mix(input) % 32),
            .next = (uint8_t)(mix(input) % 32), .target = (uint16_t)(mix(input) % 4),
            .attacker = (uint16_t)(mix(input) % 4), .pc = 384,
            .wait = (uint8_t)mix(input), .invisibility = (uint16_t)(mix(input) % 2),
            .pixelX = 100, .pixelY = 100, .tileX = 6, .tileY = 6,
            .direction = (uint8_t)(mix(input) % 8), .sound = 13
        };
        exportUnit(slot);
    }
    seed = mix(input);
    wr32(&cpu, 0x50D7C, seed);
    cases++;
}

static void transitions(void)
{
    uint32_t input = 0xD151A7C;
    for (unsigned n = 0; n < 12000; n++)
    {
        prepare(&input);
        const uint8_t pending = (uint8_t)(mix(&input) % 32);
        const uint8_t next = (uint8_t)(mix(&input) % 32);
        switch (n % 6)
        {
            case 0:
                invoke(0x2EF40, UNITS, pending, 0);
                wor_replace(&runtime, 0, pending);
                break;
            case 1:
                invoke(0x2EF9C, UNITS, pending, 0);
                wor_queue(&runtime, 0, pending, WOR_NONE);
                break;
            case 2:
                invoke(0x2EFC4, UNITS, pending, next);
                wor_queue(&runtime, 0, pending, next);
                break;
            case 3:
            {
                const bool allowNext = (n & 8) != 0;
                invoke(0x2EE9C, UNITS, allowNext, 0);
                const bool changed = wor_consume(&runtime, 0, allowNext) == WOR_CHANGED;
                equal("consumed", (cpu.r[EAX] & 0xFFFF) != 0, changed);
                break;
            }
            case 4:
                invoke(0x2216C, UNITS, 0, 0);
                equal("valid target", (cpu.r[EAX] & 0xFFFF) != 0, wor_validateTarget(&runtime, 0));
                break;
            case 5:
                invoke(0x10CE0, UNITS, UNITS + 0x5C, 0);
                wor_noteAttacker(&runtime, 0, 1);
                break;
        }
        for (unsigned slot = 0; slot < 3; slot++) compareUnit(slot);
    }
}

static void scriptControl(void)
{
    static const uint8_t programs[][12] = {
        { 4, 17, 6, 255, 9, 7, 2, 0, 3, 128, 1 },
        { 1, 0, 0, 3, 128, 1 },
        { 10, 255, 0, 3, 128, 1 },
        { 2, 0, 3, 128, 1 },
        { 9, 0, 3, 128, 1 },
        { 3, 96, 1 },
        { 5, 3, 0, 3, 128, 1 },
        { 7, 0, 0, 3, 128, 1 }
    };
    uint32_t input = 0x12345678;
    for (unsigned program = 0; program < sizeof(programs) / sizeof(*programs); program++)
    {
        for (unsigned speed = 0; speed < 3; speed++)
        {
            for (unsigned variant = 0; variant < 16; variant++)
            {
                const bool suppressed = (variant & 1) != 0;
                prepare(&input);
                units[0].current = suppressed ? 6 : 10;
                units[0].flags = suppressed ? WOR_SUPPRESS_ACTION : 0;
                units[0].direction = (uint8_t)(variant / 2);
                units[0].pixelX = INT16_MAX;
                units[0].pixelY = INT16_MIN;
                units[0].invisibility = 3;
                units[0].wait = 1;
                exportUnit(0);
                scripts[352] = 0;
                scripts[353] = 3;
                scripts[354] = 128;
                scripts[355] = 1;
                memcpy(scripts + 384, programs[program], sizeof(programs[program]));
                memcpy(cpu.mem + SCRIPTS, scripts, sizeof(scripts));
                wr8(&cpu, 0x5A856, speed);
                wr8(&cpu, 0x5A857, speed != 0 ? 0x20 : 0);
                wr32(&cpu, 0x50F7C, UNITS);
                for (unsigned tick = 0; tick < 520; tick++)
                {
                    invoke(0x10473, UNITS, 0, 0);
                    WarOrderResult result = wor_scriptTick(&runtime, 0, (uint8_t)speed, speed != 0);
                    equal("checkpoint", (cpu.r[EAX] & 0xFFFF) != 0, result == WOR_CHANGED);
                    compareUnit(0);
                    scriptTicks++;
                }
            }
        }
    }
}

static void deathTransitions(void)
{
    uint32_t input = 0xDEA7B0D1;
    for (unsigned type = 0; type <= 53; type++)
    {
        // The fire elemental also allocates an effect; building cleanup is
        // outside this isolated mobile-record comparison.
        if (type == 23 || (type >= 32 && type != 51)) continue;
        for (unsigned variant = 0; variant < 20; variant++)
        {
            prepare(&input);
            units[0].type = (uint8_t)type;
            units[0].current = 1;
            units[0].flags = (uint16_t)(WOR_DYING | ((variant & 1) ? 0x400 : 0));
            units[0].frame = 9;
            units[0].workFlags = (uint8_t)variant;
            exportUnit(0);
            invoke(0x13F68, UNITS, 0, 0);
            wor_finishDeath(&runtime, 0);
            for (unsigned slot = 0; slot < 3; slot++) compareUnit(slot);
        }
    }
    for (unsigned type = 0; type <= 53; type++)
    {
        for (unsigned animation = 0; animation < 6; animation++)
        {
            prepare(&input);
            units[0].type = (uint8_t)type;
            exportUnit(0);
            invoke(0x26450, UNITS, animation, 0);
            wor_selectAnimation(&runtime, 0, (uint8_t)animation);
            compareUnit(0);
        }
    }
}

static void exploration(void)
{
    uint32_t input = 0xF0912345;
    WarOriginalVision vision;
    wor_clearUnits(units);
    for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++) exportUnit(slot);
    wr32(&cpu, 0x596D0, UNITS);
    wr32(&cpu, 0x58608, FOG);
    static const uint16_t flags[] = { 0, 0x20, 0x100, 0x120, WOR_INSIDE, WOR_DYING, WOR_FREE, WOR_PLACEHOLDER };
    static const uint8_t orders[] = { 2, 3, 5, 6, 7, 8, 26, 27 };
    for (unsigned type = 0; type <= 50; type++)
        for (unsigned variant = 0; variant < 40; variant++)
        {
            prepare(&input);
            WarOriginalUnit* unit = &units[0];
            unit->type = (uint8_t)type;
            unit->owner = (uint8_t)(variant % 5);
            unit->flags = flags[(variant / 5) % 8];
            unit->current = orders[variant % 8];
            unit->target = (uint16_t)(variant % 3);
            const uint8_t width = (uint8_t)rd16(&cpu, 0x51ABC + type * 4);
            const uint8_t height = (uint8_t)rd16(&cpu, 0x51ABE + type * 4);
            unit->tileX = variant % 3 == 0 ? 0 : variant % 3 == 1 ? 32 : 64 - width;
            unit->tileY = variant % 4 == 0 ? 0 : variant % 4 == 1 ? 32 : 64 - height;
            const bool human = (variant & 1) != 0, network = (variant & 2) != 0;
            wr8(&cpu, 0x5008C + unit->owner, human ? 0 : 1);
            wr16(&cpu, 0x524C6, network);
            exportUnit(0);
            for (unsigned tile = 0; tile < sizeof(vision.tiles); tile++)
            {
                uint16_t first = variant % 4 ? 10 : (uint16_t)(mix(&input) % 11);
                uint16_t second = variant % 4 ? 10 : (uint16_t)(mix(&input) % 11);
                wr16(&cpu, FOG + tile * 2, first | (second << 8));
                vision.tiles[tile] = (first != 10 ? 1 : 0) | (second != 10 ? 2 : 0);
            }
            invoke(0x2EB3C, UNITS, 0, 0);
            wor_explore(&runtime, &vision, 0, width, height, human, network);
            compareUnit(0);
            for (unsigned tile = 0; tile < sizeof(vision.tiles); tile++)
            {
                uint16_t fog = rd16(&cpu, FOG + tile * 2);
                unsigned expected = ((fog & 0x1F) != 10 ? 1 : 0) | ((fog & 0x1F00) != 0xA00 ? 2 : 0);
                equal("explored tile", expected, vision.tiles[tile]);
            }
            const bool revealAll = variant % 7 == 0;
            wr32(&cpu, 0x51214, revealAll);
            invoke(0x2D740, 0, 0, 0);
            for (unsigned slot = 0; slot < 3; slot++)
            {
                unsigned size = 0x51ABC + units[slot].type * 4;
                wor_refreshVisibility(&runtime, &vision, (uint16_t)slot,
                                      (uint8_t)rd16(&cpu, size), (uint8_t)rd16(&cpu, size + 2), revealAll);
                compareUnit(slot);
            }
        }
}

static void attackZones(void)
{
    uint32_t input = 0x20AE1234;
    for (unsigned n = 0; n < 200; n++)
    {
        cases++;
        for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++)
        {
            units[slot] = (WarOriginalUnit){
                .type = (uint8_t)(mix(&input) % 51), .owner = (uint8_t)(mix(&input) % 5),
                .flags = (uint16_t)(mix(&input) & 0x208),
                .tileX = (int16_t)(mix(&input) % 64), .tileY = (int16_t)(mix(&input) % 64)
            };
            if (n == 0) units[slot].flags = WOR_FREE;
            exportUnit(slot);
        }
        invoke(0x29734, 0, 0, 0);
        int16_t x, y;
        wor_attackZone(&runtime, &x, &y);
        equal("attack zone x", rd16(&cpu, 0x50DEA), (uint16_t)x);
        equal("attack zone y", rd16(&cpu, 0x50DEC), (uint16_t)y);
    }
}

static void linePaths(void)
{
    static const int16_t deltas[] = {
        0, 1, -1, 2, -2, 3, -3, 7, -7, 16, -16, 31, -31, 511, -511, 1023, -1023
    };
    for (unsigned x = 0; x < sizeof(deltas) / sizeof(*deltas); x++)
        for (unsigned y = 0; y < sizeof(deltas) / sizeof(*deltas); y++)
        {
            cases++;
            const int16_t sx = 512, sy = 512;
            const int16_t gx = sx + deltas[x], gy = sy + deltas[y];
            const uint32_t from = (uint16_t)sx | (uint32_t)(uint16_t)sy << 16;
            const uint32_t to = (uint16_t)gx | (uint32_t)(uint16_t)gy << 16;
            memset(cpu.mem + LINE, 0, 16);
            invoke(0x1DFFC, from, to, LINE);
            WarOriginalLine line;
            wor_lineInit(&line, sx, sy, gx, gy);
            equal("line major", rd16(&cpu, LINE + 2), line.major);
            equal("line minor", rd16(&cpu, LINE + 4), line.minor);
            equal("line error", rd16(&cpu, LINE), line.err);
            for (unsigned step = 0; step < 128; step++)
            {
                int32_t dx, dy;
                invoke(0x1E084, LINE, LINE + 8, LINE + 10);
                const uint8_t direction = wor_lineStep(&line, &dx, &dy);
                equal("step x", rd16(&cpu, LINE + 8), (uint16_t)dx);
                equal("step y", rd16(&cpu, LINE + 10), (uint16_t)dy);
                equal("step direction", cpu.r[EAX] & 0xFF, direction);
                equal("step error", rd16(&cpu, LINE), (uint16_t)line.err);
            }
        }
}

static void replacementPosts(void)
{
    uint32_t input = 0x90571234;
    wr32(&cpu, 0x596D0, UNITS);
    for (unsigned n = 0; n < 400; n++)
    {
        cases++;
        wor_clearUnits(units);
        units[0] = (WarOriginalUnit){
            .type = (uint8_t)(n % 4), .owner = (uint8_t)(1 + n % 3),
            .postX = -1, .postY = -1
        };
        for (unsigned i = 1; i < WOR_UNIT_COUNT; i++)
        {
            WarOriginalUnit* post = &units[i];
            post->flags = n % 5 ? WOR_PLACEHOLDER : WOR_DYING;
            post->owner = (uint8_t)(mix(&input) % 5);
            post->type = (uint8_t)(mix(&input) % 54);
            post->postType = (uint16_t)(mix(&input) % 8);
            post->postX = (int16_t)(mix(&input) % 64);
            post->postY = (int16_t)(mix(&input) % 64);
            if (n % 5 == 1) post->type = 32;
            if (n % 5 == 2) post->postType = 0x100;
            if (n % 5 == 3)
            {
                post->owner = units[0].owner;
                post->type = 51;
                post->postType = units[0].type;
            }
        }
        for (unsigned i = 0; i < WOR_UNIT_COUNT; i++) exportUnit(i);
        bool alerted = (n & 1) != 0;
        wr16(&cpu, 0x50E5E + units[0].owner * 2, alerted);
        seed = mix(&input);
        wr32(&cpu, 0x50D7C, seed);
        invoke(0x29B8C, UNITS, 0, 0);
        equal("replacement post", (cpu.r[EAX] & 0xFFFF) != 0,
              wor_takePost(&runtime, 0, &alerted) == WOR_CHANGED);
        equal("post alert cache", rd16(&cpu, 0x50E5E + units[0].owner * 2), alerted);
        for (unsigned i = 0; i < WOR_UNIT_COUNT; i++) compareUnit(i);
    }
}

static void nearbyTrees(void)
{
    uint8_t trees[WOR_MAP_WIDTH * WOR_MAP_WIDTH];
    uint32_t input = 0x7EEE1234;
    wr32(&cpu, 0x58614, TERRAIN);
    wr32(&cpu, 0x55098, TREE_ATTRIBUTES);
    wr16(&cpu, 0x5A4D4, 0x100);
    wr16(&cpu, 0x5A4C0, 1);
    wr16(&cpu, 0x5A4C2, 1);
    wr16(&cpu, TREE_ATTRIBUTES + 2, 0);
    for (unsigned n = 0; n < 200; n++)
    {
        cases++;
        const int32_t x = n % 3 == 0 ? 0 : n % 3 == 1 ? 32 : 63;
        const int32_t y = n % 4 == 0 ? 0 : n % 4 == 1 ? 32 : 63;
        for (unsigned tile = 0; tile < sizeof(trees); tile++)
        {
            trees[tile] = n > 1 && mix(&input) % 47 == 0;
            if (n == 1 && tile == (unsigned)(y * WOR_MAP_WIDTH + x)) trees[tile] = 1;
            wr16(&cpu, TERRAIN + tile * 2, trees[tile] ? 0x100 : 0);
        }
        wr16(&cpu, LINE, (uint16_t)x);
        wr16(&cpu, LINE + 2, (uint16_t)y);
        invoke(0x2BCD4, LINE, 0, 0);
        int32_t tx, ty;
        const bool found = wor_treeAround(trees, x, y, &tx, &ty);
        equal("nearby tree found", cpu.r[EAX], found);
        if (found)
        {
            equal("nearby tree x", rd16(&cpu, LINE), (uint16_t)tx);
            equal("nearby tree y", rd16(&cpu, LINE + 2), (uint16_t)ty);
        }
    }
}

static bool probeSummon(WarOriginalRuntime* state, uint16_t caster, uint8_t type)
{
    (void)state; (void)type;
    equal("summon caster", 0, caster);
    return false;
}

static uint16_t occupiedTile(WarOriginalRuntime* state, int16_t x, int16_t y)
{
    const uint16_t* grid = state->context;
    return grid[y * WOR_MAP_WIDTH + x];
}

static void catapultSafety(void)
{
    uint32_t input = 0x11140;
    uint16_t grid[WOR_MAP_WIDTH * WOR_MAP_WIDTH];
    runtime.context = grid;
    runtime.occupant = occupiedTile;
    wr32(&cpu, 0x58610, OCCUPANCY);
    for (unsigned n = 0; n < 512; n++)
    {
        prepare(&input);
        units[0].type = 4;
        units[0].current = 8;
        units[0].flags = 0;
        units[0].target = 2;
        units[0].owner = (uint8_t)(n % 5);
        units[1].type = n & 1 ? 32 : 6;
        units[1].tileX = n & 16 ? 31 : n & 2 ? 0 : 63;
        units[1].tileY = n & 32 ? 32 : n & 4 ? 0 : 63;
        units[1].owner = units[0].owner == 0 ? 1 : 0;
        units[2].owner = 4;
        units[3].owner = units[0].owner;
        units[0].tileX = (int16_t)(units[1].tileX + (int)(n % 9) - 4);
        units[0].tileY = (int16_t)(units[1].tileY + (int)(n / 9 % 9) - 4);
        const uint8_t controller = (uint8_t)(n % 3);
        wr8(&cpu, 0x5008C + units[0].owner, controller);
        for (unsigned i = 0; i < 4; i++) exportUnit(i);
        for (unsigned i = 0; i < WOR_MAP_WIDTH * WOR_MAP_WIDTH; i++)
        {
            grid[i] = n % 8 ? (mix(&input) % 8 ? 0 : (uint16_t)(2 + n % 3)) : 0;
            wr32(&cpu, OCCUPANCY + i * 4, grid[i] ? UNITS + (grid[i] - 1) * 0x5C : 0);
        }
        const uint32_t footprint = 0x51ABC + units[1].type * 4;
        const uint8_t width = (uint8_t)rd16(&cpu, footprint);
        const uint8_t height = (uint8_t)rd16(&cpu, footprint + 2);
        invoke(0x11140, UNITS, 0, 0);
        equal("catapult cancellation", cpu.r[EAX] & 0xFFFF,
              wor_checkCatapult(&runtime, 0, controller, rd8(&cpu, 0x511E1),
                                width, height) == WOR_CHANGED);
        compareUnit(0);
    }
    runtime.context = NULL;
    runtime.occupant = NULL;
}

static void summoning(void)
{
    uint32_t input = 0x27AB0;
    runtime.summon = probeSummon;
    // Full terrain blocks the original factory before allocation or effects.
    wr32(&cpu, 0x5860C, TERRAIN);
    for (unsigned tile = 0; tile < WOR_MAP_WIDTH * WOR_MAP_WIDTH; tile++)
        wr16(&cpu, TERRAIN + tile * 2, 0x3CC);
    for (unsigned order = 22; order <= 25; order++)
        for (unsigned mana = 0; mana < 256; mana++)
            for (unsigned action = 0; action < 2; action++)
            {
                prepare(&input);
                units[0].current = (uint8_t)order;
                units[0].type = 10;
                exportUnit(0);
                wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
                invoke(0x27AB0, UNITS, action, 0);
                const bool accepted = (cpu.r[EAX] & 0xFFFF) != 0;
                uint8_t remaining = (uint8_t)mana;
                equal("summoning accepted", accepted,
                      wor_summon(&runtime, 0, &remaining, action != 0) == WOR_CHANGED);
                equal("summoning mana", rd8(&cpu, UNITS + 0x1A), remaining);
                compareUnit(0);
            }
    runtime.summon = NULL;
}

static bool exitBlocked(WarOriginalRuntime* state, int16_t x, int16_t y)
{
    const uint16_t* flags = state->context;
    return (flags[y * WOR_MAP_WIDTH + x] & 0x3CC) != 0;
}

static void exitSearch(void)
{
    uint16_t flags[WOR_MAP_WIDTH * WOR_MAP_WIDTH];
    uint32_t input = 0x2E554;
    runtime.context = flags;
    runtime.blocked = exitBlocked;
    wr32(&cpu, 0x5860C, TERRAIN);
    for (unsigned n = 0; n < 512; n++)
    {
        cases++;
        for (unsigned tile = 0; tile < WOR_MAP_WIDTH * WOR_MAP_WIDTH; tile++)
        {
            flags[tile] = n % 8 ? (uint16_t)(mix(&input) & 0x3FF) : 0x3CC;
            wr16(&cpu, TERRAIN + tile * 2, flags[tile]);
        }
        int16_t x = (int16_t)(mix(&input) % 64), y = (int16_t)(mix(&input) % 64);
        if (n % 4 == 0) x = 0;
        if (n % 4 == 1) y = 63;
        const uint8_t width = (uint8_t)(1 + n % 4), height = (uint8_t)(1 + n / 4 % 4);
        const int16_t tx = (int16_t)(x + (n & 1 ? 1 : -1));
        const int16_t ty = (int16_t)(y + (n & 2 ? 1 : -1));
        const int16_t sx = x, sy = y;
        wr16(&cpu, LINE, (uint16_t)x);
        wr16(&cpu, LINE + 2, (uint16_t)y);
        invoke4(0x2E554, LINE, LINE + 2, width | (uint32_t)height << 16,
                (uint16_t)tx | (uint32_t)(uint16_t)ty << 16);
        const bool found = (cpu.r[EAX] & 0xFFFF) != 0;
        const bool actual = wor_findExit(&runtime, &x, &y, width, height, tx, ty) == WOR_CHANGED;
        if (found != actual)
            fprintf(stderr, "exit case %u: %d,%d size %u,%u toward %d,%d; kernel %d,%d\n",
                    n, sx, sy, width, height, tx, ty, x, y);
        equal("exit found", found, actual);
        equal("exit x", rd16(&cpu, LINE), (uint16_t)x);
        equal("exit y", rd16(&cpu, LINE + 2), (uint16_t)y);
    }
    runtime.context = NULL;
    runtime.blocked = NULL;
}

static void healingAndArmour(void)
{
    uint32_t input = 0x27760;
    static const uint8_t types[] = {0, 28, 32, 51};
    static const uint16_t healthValues[] = {0, 1, 29, 30, 31, 39, 40, 41, 60, 61, 100, 65535};
    wr32(&cpu, 0x596D4, 0x238000);
    memset(cpu.mem + 0x238000, 0, 100 * 0x26);
    for (unsigned spell = 0; spell < 2; spell++)
        for (unsigned mana = 0; mana < 256; mana++)
            for (unsigned action = 0; action < 2; action++)
                for (unsigned type = 0; type < sizeof(types); type++)
                    for (unsigned hp = 0; hp < sizeof(healthValues) / sizeof(*healthValues); hp++)
                    {
                        prepare(&input);
                        units[0].type = spell ? 13 : 12;
                        units[0].target = 2;
                        units[1].type = types[type];
                        units[1].invulnerability = (uint16_t)(hp * 100);
                        exportUnit(0); exportUnit(1);
                        wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
                        wr16(&cpu, UNITS + 0x5C + 0x16, healthValues[hp]);
                        invoke(spell ? 0x278C4 : 0x27760, UNITS, action, 0);
                        uint8_t remaining = (uint8_t)mana;
                        uint16_t health = healthValues[hp];
                        const uint16_t base = rd16(&cpu, 0x51978 + types[type] * 2);
                        WarOrderResult result = spell ?
                            wor_unholyArmour(&runtime, 0, &remaining, &health, action != 0) :
                            wor_healing(&runtime, 0, &remaining, &health, base, action != 0);
                        equal("spell eligibility", (cpu.r[EAX] & 0xFFFF) != 0, result == WOR_CHANGED);
                        equal("spell mana", rd8(&cpu, UNITS + 0x1A), remaining);
                        equal("spell health", rd16(&cpu, UNITS + 0x5C + 0x16), health);
                        compareUnit(0); compareUnit(1);
                    }
}

static bool probeRaiseCorpse(WarOriginalRuntime* state, uint16_t caster, uint16_t corpse)
{
    equal("resurrection caster", 0, caster);
    lastCorpseDirection = state->units[corpse].direction;
    return false;
}

// Compare full selection and blocked creation; successful creation also has
// portable callback and real-game coverage.
static void resurrection(void)
{
    static const int16_t offsets[][2] = {
        {0, 6}, {0, 7}, {6, 0}, {6, 1}, {9, 0}, {10, 0}, {5, 3}, {4, 4}, {3, 5}
    };
    static const uint16_t flags[] = {2, 0x82, 0x402, 0xA, 6, 3, 0x202, 0};
    uint32_t input = 0x2799C;
    runtime.resurrect = probeRaiseCorpse;
    wr16(&cpu, 0x500D0, 32767);
    wr32(&cpu, 0x5860C, TERRAIN);
    wr32(&cpu, 0x58610, OCCUPANCY);
    for (unsigned mana = 0; mana < 256; mana++)
        for (unsigned action = 0; action < 2; action++)
            for (unsigned layout = 0; layout < 4; layout++)
            {
                wor_clearUnits(units);
                prepare(&input);
                units[0] = (WarOriginalUnit){
                    .type = 13, .owner = (uint8_t)layout, .tileX = 32, .tileY = 32,
                    .goalX = 56, .goalY = 56, .current = 20, .pending = WOR_NONE, .next = WOR_NONE
                };
                for (unsigned n = 0; n < 9; n++)
                {
                    const unsigned slot = n == 8 ? 199 : n + 1;
                    units[slot] = (WarOriginalUnit){
                        .type = layout == 3 && n == 0 ? 0 : 51,
                        .flags = flags[(n + layout) % 8], .owner = 4,
                        .tileX = (int16_t)(32 + (layout & 1 ? -1 : 1) * offsets[n][0]),
                        .tileY = (int16_t)(32 + (layout & 2 ? -1 : 1) * offsets[n][1]),
                        .direction = (uint8_t)(n % 8)
                    };
                }
                for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++) exportUnit(slot);
                memset(cpu.mem + TERRAIN, 0, WOR_MAP_WIDTH * WOR_MAP_WIDTH * 2);
                memset(cpu.mem + OCCUPANCY, 0, WOR_MAP_WIDTH * WOR_MAP_WIDTH * 4);
                invoke(0x2A4BC, UNITS, 0, 0);
                const uint16_t selected = wor_findCorpse(&runtime, 0);
                equal("corpse search found", cpu.r[EAX] != 0, selected != 0);
                if (selected)
                {
                    equal("corpse search x", rd16(&cpu, UNITS + 0x4E), (uint16_t)units[selected - 1].tileX);
                    equal("corpse search y", rd16(&cpu, UNITS + 0x50), (uint16_t)units[selected - 1].tileY);
                }
                for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++) exportUnit(slot);
                for (unsigned tile = 0; tile < WOR_MAP_WIDTH * WOR_MAP_WIDTH; tile++)
                    wr16(&cpu, TERRAIN + tile * 2, 0x3CC);
                lastCorpseDirection = 255;
                wr8(&cpu, 0x5A858, 255);
                wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
                invoke(0x2799C, UNITS, action, 0);
                uint8_t remaining = (uint8_t)mana;
                equal("resurrection accepted", cpu.r[EAX] != 0,
                    wor_resurrect(&runtime, 0, &remaining, action != 0) == WOR_CHANGED);
                equal("resurrection mana", rd8(&cpu, UNITS + 0x1A), remaining);
                equal("last corpse facing", rd8(&cpu, 0x5A858), lastCorpseDirection);
                compareUnit(0);
                for (unsigned n = 0; n < 9; n++)
                {
                    const unsigned slot = n == 8 ? 199 : n + 1;
                    if (!(units[slot].flags & WOR_FREE)) compareUnit(slot);
                }
            }
    runtime.resurrect = NULL;
}

static void facing(void)
{
    for (int16_t dx = -63; dx <= 63; dx++)
        for (int16_t dy = -63; dy <= 63; dy++)
        {
            cases++;
            invoke(0x1DF50, 0, (uint16_t)dx | (uint32_t)(uint16_t)dy << 16, 0);
            equal("tile facing", cpu.r[EAX] & 0xFF, wor_facing(dx, dy));
        }
}

static void protectionTimers(void)
{
    static const uint16_t timers[] = {0, 1, 999, 1000, 3000, 65535};
    static const uint16_t flags[] = {0, WOR_INSIDE, WOR_DYING, WOR_REMOVED, WOR_FREE, WOR_PLACEHOLDER};
    wor_clearUnits(units);
    for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++) exportUnit(slot);
    for (unsigned flag = 0; flag < sizeof(flags) / sizeof(*flags); flag++)
        for (unsigned first = 0; first < sizeof(timers) / sizeof(*timers); first++)
            for (unsigned second = 0; second < sizeof(timers) / sizeof(*timers); second++)
            {
                cases++;
                units[0] = (WarOriginalUnit){
                    .type = 0, .flags = flags[flag],
                    .invisibility = timers[first], .invulnerability = timers[second]
                };
                exportUnit(0);
                seed = cases * 31u;
                wr32(&cpu, 0x50D7C, seed);
                if (!(randomValue(&runtime) & 15)) { randomValue(&runtime); randomValue(&runtime); }
                invoke(0x2D3D0, 0, 0, 0);
                wor_tickSpellTimers(&runtime);
                compareUnit(0);
            }
}

static void manaRecovery(void)
{
    static const uint8_t waits[] = {0, 1, 40, 60};
    wor_clearUnits(units);
    for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++) exportUnit(slot);
    for (unsigned mana = 0; mana < 256; mana++)
        for (unsigned controller = 0; controller < 3; controller++)
            for (unsigned wait = 0; wait < 4; wait++)
            {
                cases++;
                units[0] = (WarOriginalUnit){
                    .type = (uint8_t)(10 + mana % 5), .owner = (uint8_t)(mana % 5),
                    .current = 2, .pending = WOR_NONE, .next = WOR_NONE,
                    .flags = mana % 11 ? 0 : WOR_DYING, .statusTicks = waits[wait]
                };
                exportUnit(0);
                wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
                wr8(&cpu, 0x5008C + units[0].owner, (uint8_t)controller);
                seed = cases * 31u;
                wr32(&cpu, 0x50D7C, seed);
                if (!(randomValue(&runtime) & 15)) { randomValue(&runtime); randomValue(&runtime); }
                invoke(0x2D3D0, 0, 0, 0);
                uint8_t remaining = (uint8_t)mana;
                wor_regenerateMana(&runtime, 0, (uint8_t)controller, &remaining);
                equal("recovered mana", rd8(&cpu, UNITS + 0x1A), remaining);
                compareUnit(0);
            }
}

static void shotPayment(void)
{
    uint32_t input = 0x106EC;
    wr32(&cpu, 0x596D4, 0x238000);
    memset(cpu.mem + 0x238000, 0, 100 * 0x26);
    for (unsigned type = 10; type <= 14; type++)
        for (unsigned controller = 0; controller < 3; controller++)
            for (unsigned mana = 0; mana < 256; mana++)
            {
                prepare(&input);
                units[0].type = (uint8_t)type;
                units[0].owner = 1;
                units[0].target = 2;
                exportUnit(0);
                wr8(&cpu, 0x5008D, (uint8_t)controller);
                wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
                invoke(0x106EC, UNITS, 0, 0);
                uint8_t remaining = (uint8_t)mana;
                equal("shot payment accepted", WOR_CHANGED,
                      wor_shotMana(&runtime, 0, (uint8_t)controller, &remaining, true));
                equal("failed allocation shot payment", rd8(&cpu, UNITS + 0x1A), remaining);
                compareUnit(0);
            }
}

static void summonDecay(void)
{
    static const uint8_t waits[] = {0, 1, 45, 255};
    static const uint16_t flags[] = {0, 0x400, WOR_INSIDE, WOR_DYING};
    wor_clearUnits(units);
    for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++) exportUnit(slot);
    for (unsigned type = 19; type < 32; type++)
        for (unsigned state = 0; state < 4; state++)
            for (unsigned wait = 0; wait < 4; wait++)
                for (unsigned mana = 0; mana < 3; mana++)
                {
                    cases++;
                    units[0] = (WarOriginalUnit){
                        .type = (uint8_t)type, .owner = 1, .flags = flags[state],
                        .current = 2, .pending = WOR_NONE, .next = WOR_NONE,
                        .statusTicks = waits[wait]
                    };
                    exportUnit(0);
                    wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
                    wr16(&cpu, UNITS + 0x16, 5);
                    seed = cases * 31u;
                    wr32(&cpu, 0x50D7C, seed);
                    if (!(randomValue(&runtime) & 15)) { randomValue(&runtime); randomValue(&runtime); }
                    invoke(0x2D3D0, 0, 0, 0);
                    uint8_t remaining = (uint8_t)mana;
                    uint16_t health = 5;
                    bool dead;
                    wor_decay(&runtime, 0, &remaining, &health, &dead);
                    equal("decay mana", rd8(&cpu, UNITS + 0x1A), remaining);
                    equal("decay health", rd16(&cpu, UNITS + 0x16), health);
                    equal("decay alive", 0, dead);
                    compareUnit(0);
                }
}

static void invisibility(void)
{
    uint32_t input = 0x27890;
    // A full effect pool isolates the spell's state changes from presentation.
    wr32(&cpu, 0x596D4, 0x238000);
    memset(cpu.mem + 0x238000, 0, 100 * 0x26);
    for (unsigned mana = 0; mana < 256; mana++)
        for (unsigned action = 0; action < 2; action++)
        {
            prepare(&input);
            units[0].target = 2;
            units[1].invisibility = 700;
            exportUnit(0);
            exportUnit(1);
            wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
            invoke(0x27890, UNITS, action, 0);
            const bool accepted = (cpu.r[EAX] & 0xFFFF) != 0;
            uint8_t remaining = (uint8_t)mana;
            equal("invisibility accepted", accepted,
                  wor_invisibility(&runtime, 0, &remaining, action != 0) == WOR_CHANGED);
            equal("invisibility mana", rd8(&cpu, UNITS + 0x1A), remaining);
            compareUnit(0);
            compareUnit(1);
        }
}

static void terrainDamage(void)
{
    uint16_t flags[WOR_MAP_WIDTH * WOR_MAP_WIDTH], visual[WOR_MAP_WIDTH * WOR_MAP_WIDTH];
    uint8_t table[4 + 9 * 3 * 2] = { 3, 0, 9, 0 };
    WarOriginalTerrain terrain = { flags, visual, table, sizeof(table), false };
    wr32(&cpu, 0x5860C, TERRAIN);
    wr32(&cpu, 0x58614, VISUAL);
    wr32(&cpu, 0x55090, WALL_TABLE + 4);
    wr16(&cpu, 0x5A4C4, 3);
    wr16(&cpu, 0x5A4C6, 8);
    wr16(&cpu, 0x5A4D2, 10);
    wr16(&cpu, 0x500D0, 0xFFFF); // Keep renderer calls outside this simulation comparison.
    static const uint8_t damage[] = { 0, 1, 2, 37, 38, 39, 254, 255 };
    for (unsigned n = 0; n < 512; n++)
    {
        cases++;
        const int32_t x = n % 13 == 0 ? -1 : n % 13 == 1 ? 64 : 32;
        const int32_t y = 32;
        terrain.linkedBreaks = (n & 8) != 0;
        wr16(&cpu, 0x55146, terrain.linkedBreaks);
        for (unsigned row = 1; row < 9; row++)
        {
            const uint16_t transition = (uint16_t)((row + n) % 9 |
                ((n & 1) ? 0x1000 : 0) | ((n & 2) ? 0x2000 : 0));
            const unsigned offset = 4 + (row * 3 + 1) * 2;
            table[offset] = (uint8_t)transition;
            table[offset + 1] = (uint8_t)(transition >> 8);
        }
        memcpy(cpu.mem + WALL_TABLE, table, sizeof(table));
        for (unsigned i = 0; i < WOR_MAP_WIDTH * WOR_MAP_WIDTH; i++)
        {
            flags[i] = (uint16_t)(0x190 | (n % 11 ? 0xC : 0));
            visual[i] = (uint16_t)(((n / 8) % 32) << 11 | 9 + (i + n) % 11);
            wr16(&cpu, TERRAIN + i * 2, flags[i]);
            wr16(&cpu, VISUAL + i * 2, visual[i]);
        }
        invoke(0x105D0, (uint32_t)x, (uint32_t)y, damage[n % 8]);
        wor_damageTerrain(&runtime, &terrain, x, y, damage[n % 8]);
        equal("linked break flag", rd16(&cpu, 0x55146), terrain.linkedBreaks);
        for (unsigned i = 0; i < WOR_MAP_WIDTH * WOR_MAP_WIDTH; i++)
        {
            equal("terrain flags", rd16(&cpu, TERRAIN + i * 2), flags[i]);
            equal("terrain visual", rd16(&cpu, VISUAL + i * 2), visual[i]);
        }
    }
}

static void wind(void)
{
    wor_clearUnits(units);
    for (unsigned slot = 0; slot < WOR_UNIT_COUNT; slot++) exportUnit(slot);
    int8_t x = 0, y = 0;
    wr8(&cpu, 0x551C4, 0);
    wr8(&cpu, 0x551C5, 0);
    seed = 0x2D3D0;
    wr32(&cpu, 0x50D7C, seed);
    for (unsigned n = 0; n < 20000; n++)
    {
        cases++;
        invoke(0x2D3D0, 0, 0, 0);
        wor_tickWind(&runtime, &x, &y);
        equal("wind X", rd8(&cpu, 0x551C4), (uint8_t)x);
        equal("wind Y", rd8(&cpu, 0x551C5), (uint8_t)y);
        equal("wind seed", rd32(&cpu, 0x50D7C), seed);
    }
}

static void unexpectedPoisonDamage(WarOriginalRuntime* state, uint16_t source, uint16_t target, uint8_t damage)
{
    (void)source; (void)damage;
    kernelError(state, target, "damage in empty poison fixture");
}

static void areaTargets(void)
{
    uint16_t grid[WOR_MAP_WIDTH * WOR_MAP_WIDTH];
    uint32_t input = 0x2A864;
    runtime.context = grid;
    runtime.occupant = occupiedTile;
    wr32(&cpu, 0x58610, OCCUPANCY);
    wr32(&cpu, 0x5860C, TERRAIN);
    memset(cpu.mem + TERRAIN - 512, 0, WOR_MAP_WIDTH * WOR_MAP_WIDTH * 2 + 1024);
    static const uint16_t flags[] = {0, WOR_DYING, WOR_REMOVED, WOR_FREE, WOR_PLACEHOLDER, WOR_INSIDE};
    for (unsigned n = 0; n < 1024; n++)
    {
        cases++;
        int16_t tx = n % 7 == 0 ? -1 : n % 7 == 1 ? 0 : n % 7 == 2 ? 63 : 32;
        int16_t ty = n % 11 == 0 ? -1 : n % 11 == 1 ? 0 : n % 11 == 2 ? 63 : 32;
        units[0] = (WarOriginalUnit){
            .type = 11, .owner = (uint8_t)(n % 5), .current = 2, .pending = WOR_NONE, .next = WOR_NONE,
            .tileX = n & 1 ? tx : 6, .tileY = n & 2 ? ty : 7, .goalX = 40, .goalY = 41
        };
        for (unsigned slot = 1; slot < 4; slot++)
            units[slot] = (WarOriginalUnit){
                .type = slot == 1 ? 32 : 0, .owner = (uint8_t)((n + slot) % 5),
                .flags = flags[(n + slot) % 6],
                .tileX = tx < 1 ? 0 : tx > 60 ? 60 : (int16_t)(tx - 7 + slot),
                .tileY = ty < 1 ? 0 : ty > 60 ? 60 : (int16_t)(ty - 7 + slot)
            };
        for (unsigned slot = 0; slot < 4; slot++) exportUnit(slot);
        for (unsigned tile = 0; tile < WOR_MAP_WIDTH * WOR_MAP_WIDTH; tile++)
        {
            grid[tile] = mix(&input) % 19 ? 0 : (uint16_t)(2 + mix(&input) % 3);
            wr32(&cpu, OCCUPANCY + tile * 4, reference(grid[tile]));
        }
        seed = mix(&input);
        wr32(&cpu, 0x50D7C, seed);
        wr16(&cpu, 0x50DEA, (uint16_t)tx);
        wr16(&cpu, 0x50DEC, (uint16_t)ty);
        wr16(&cpu, 0x5A850, 27);
        invoke(0x2A864, UNITS, 0, 0);
        const bool accepted = cpu.r[EAX] != 0;
        int16_t x = 40, y = 41;
        equal("area candidate", accepted, wor_areaTarget(&runtime, 0, tx, ty, &x, &y) == WOR_CHANGED);
        equal("area X", rd16(&cpu, UNITS + 0x4E), (uint16_t)x);
        equal("area Y", rd16(&cpu, UNITS + 0x50), (uint16_t)y);
        equal("area random state", rd32(&cpu, 0x50D7C), seed);
    }
    runtime.context = NULL;
    runtime.occupant = NULL;
}

static void poison(void)
{
    enum { EFFECTS = 0x238000 };
    WarOriginalEffect effects[WOR_EFFECT_COUNT];
    uint16_t grid[WOR_MAP_WIDTH * WOR_MAP_WIDTH] = {0};
    runtime.effects = effects;
    runtime.context = grid;
    runtime.occupant = occupiedTile;
    runtime.damage = unexpectedPoisonDamage;
    wr32(&cpu, 0x596D4, EFFECTS);
    wr32(&cpu, 0x58610, OCCUPANCY);
    memset(cpu.mem + OCCUPANCY, 0, WOR_MAP_WIDTH * WOR_MAP_WIDTH * 4);
    units[0] = (WarOriginalUnit){ .type = 11, .goalX = 32, .goalY = 32 };
    exportUnit(0);
    for (unsigned mana = 0; mana < 256; mana++)
        for (unsigned action = 0; action < 2; action++)
            for (unsigned layout = 0; layout < 3; layout++)
            {
                cases++;
                memset(effects, 0, sizeof(effects));
                memset(cpu.mem + EFFECTS, 0, WOR_EFFECT_COUNT * 0x26);
                const unsigned available = layout == 0 ? 0 : layout == 1 ? 99 : 100;
                for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++)
                {
                    effects[i].used = i != available;
                    wr8(&cpu, EFFECTS + i * 0x26 + 1, i == available);
                }
                wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
                invoke(0x27914, UNITS, action, 0);
                uint8_t remaining = (uint8_t)mana;
                equal("poison accepted", cpu.r[EAX] != 0,
                      wor_poison(&runtime, 0, &remaining, action != 0) == WOR_CHANGED);
                equal("poison payment", rd8(&cpu, UNITS + 0x1A), remaining);
                for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++)
                    equal("poison allocation", !(rd8(&cpu, EFFECTS + i * 0x26 + 1) & 1), effects[i].used);
                if (!action || mana < 50 || available == WOR_EFFECT_COUNT) continue;
                WarOriginalEffect* effect = &effects[available];
                const uint32_t address = EFFECTS + available * 0x26;
                equal("poison constructor PC", rd16(&cpu, address + 0xA), effect->pc);
                equal("poison constructor X", rd16(&cpu, address + 6), (uint16_t)effect->x);
                equal("poison constructor Y", rd16(&cpu, address + 8), (uint16_t)effect->y);
                equal("poison source", rd32(&cpu, address + 0x22), reference(effect->source));
                runtime.windX = -2;
                runtime.windY = 1;
                wr8(&cpu, 0x551C4, (uint8_t)runtime.windX);
                wr8(&cpu, 0x551C5, (uint8_t)runtime.windY);
                // A synthetic yielding program exercises the real interpreter and phases.
                const uint8_t program[] = { 4, 2, 1, 2, 0, 3, 192, 0 };
                const uint8_t cloud[] = { 4, 1, 1, 3, 0, 4, 2, 1, 5, 0, 3, 208, 0 };
                memcpy(scripts + 192, program, sizeof(program));
                memcpy(scripts + 208, cloud, sizeof(cloud));
                memcpy(cpu.mem + SCRIPTS, scripts, sizeof(scripts));
                for (unsigned tick = 0; tick < 400 && effect->used; tick++)
                {
                    invoke(0x10473, address + 6, 0, 0);
                    if (cpu.r[EAX])
                        invoke(rd8(&cpu, address + 2) == 0 ? 0x11FA8 : 0x122D0, address, 0, 0);
                    equal("poison tick error", 0, wor_poisonTick(&runtime, effect) == WOR_ERROR);
                    equal("poison lifetime", rd16(&cpu, address + 4), effect->pulses);
                    equal("poison X", rd16(&cpu, address + 6), (uint16_t)effect->x);
                    equal("poison Y", rd16(&cpu, address + 8), (uint16_t)effect->y);
                    equal("poison PC", rd16(&cpu, address + 0xA), effect->pc);
                    equal("poison wait", rd8(&cpu, address + 0xC), effect->wait);
                    equal("poison frame", rd8(&cpu, address + 0xD), effect->frame);
                    equal("poison phase", rd8(&cpu, address + 2), effect->state);
                    equal("poison release", !(rd8(&cpu, address + 1) & 1), effect->used);
                }
                equal("poison expired", 0, effect->used);
            }
    runtime.effects = NULL;
    runtime.context = NULL;
    runtime.occupant = NULL;
    runtime.damage = NULL;
}

static unsigned rainTerrainStrikes;

static void countRainTerrain(WarOriginalRuntime* state, int16_t x, int16_t y, uint8_t damage)
{
    (void)state; (void)x; (void)y; (void)damage;
    rainTerrainStrikes++;
}

static void unexpectedRainDamage(WarOriginalRuntime* state, uint16_t source, uint16_t target, uint8_t damage)
{
    (void)source; (void)damage;
    kernelError(state, target, "damage in empty rain fixture");
}

// Rain (0x2793C, 0x11CD8) and its records' falls and impacts through the
// dispatch's phases (0x11FA8, 0x12338, 0x123E0), on empty occupancy.
static unsigned rain(void)
{
    enum { EFFECTS = 0x238000 };
    static WarOriginalEffect effects[WOR_EFFECT_COUNT];
    uint16_t grid[WOR_MAP_WIDTH * WOR_MAP_WIDTH] = {0};
    unsigned rainCases = 0;
    runtime.effects = effects;
    runtime.context = grid;
    runtime.occupant = occupiedTile;
    runtime.damage = unexpectedRainDamage;
    runtime.terrainDamage = countRainTerrain;
    wr32(&cpu, 0x596D4, EFFECTS);
    wr32(&cpu, 0x58610, OCCUPANCY);
    wr32(&cpu, 0x5860C, TERRAIN);
    memset(cpu.mem + OCCUPANCY, 0, WOR_MAP_WIDTH * WOR_MAP_WIDTH * 4);
    memset(cpu.mem + TERRAIN, 0, WOR_MAP_WIDTH * WOR_MAP_WIDTH * 2);
    wr16(&cpu, 0x52130 + 4 * 2, 0);   // (no sound in the fixture: 0x1BBCC)
    wr16(&cpu, 0x52130 + 7 * 2, 0);
    const uint8_t program[] = { 4, 2, 1, 2, 0, 3, 192, 0 };
    const uint8_t fall[] = { 4, 1, 1, 1, 0, 3, 208, 0 };
    memcpy(scripts + 192, program, sizeof(program));
    memcpy(scripts + 208, fall, sizeof(fall));
    memcpy(cpu.mem + SCRIPTS, scripts, sizeof(scripts));
    for (unsigned mana = 0; mana < 256; mana++)
        for (unsigned action = 0; action < 2; action++)
            for (unsigned layout = 0; layout < 5; layout++)
            {
                cases++;
                rainCases++;
                units[0] = (WarOriginalUnit){ .type = 11, .goalX = (int16_t)(1 + mana % 62),
                                              .goalY = (int16_t)(1 + (mana * 7) % 62),
                                              .current = 26, .pending = WOR_NONE, .next = WOR_NONE };
                exportUnit(0);
                memset(effects, 0, sizeof(effects));
                memset(cpu.mem + EFFECTS, 0, WOR_EFFECT_COUNT * 0x26);
                for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++)
                {
                    // none, one, two apart, the last two, or all free
                    const bool free = layout == 4 || (layout == 1 && i == 40) ||
                        (layout == 2 && (i == 3 || i == 9 || i == 50)) || (layout == 3 && i >= 98);
                    effects[i].used = !free;
                    wr8(&cpu, EFFECTS + i * 0x26 + 1, free);
                }
                wr8(&cpu, UNITS + 0x1A, (uint8_t)mana);
                invoke(0x2793C, UNITS, action, 0);
                uint8_t remaining = (uint8_t)mana;
                equal("rain accepted", cpu.r[EAX] != 0,
                      wor_rain(&runtime, 0, &remaining, action != 0) == WOR_CHANGED);
                equal("rain payment", rd8(&cpu, UNITS + 0x1A), remaining);
                equal("rain pending", rd8(&cpu, UNITS + 0x20), units[0].pending);
                equal("rain next", rd8(&cpu, UNITS + 0x21), units[0].next);
                equal("random state", rd32(&cpu, 0x50D7C), seed);
                for (unsigned tick = 0; tick < 400; tick++)
                {
                    bool any = false;
                    for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++)
                    {
                        const uint32_t address = EFFECTS + i * 0x26;
                        WarOriginalEffect* effect = &effects[i];
                        equal("rain allocation", !(rd8(&cpu, address + 1) & 1), effect->used);
                        if (!effect->used || (effect->type != 4 && effect->type != 7)) continue;
                        any = true;
                        equal("rain type", rd8(&cpu, address), effect->type);
                        equal("rain X", rd16(&cpu, address + 6), (uint16_t)effect->x);
                        equal("rain Y", rd16(&cpu, address + 8), (uint16_t)effect->y);
                        equal("rain PC", rd16(&cpu, address + 0xA), effect->pc);
                        equal("rain wait", rd8(&cpu, address + 0xC), effect->wait);
                        equal("rain frame", rd8(&cpu, address + 0xD), effect->frame);
                        equal("rain phase", rd8(&cpu, address + 2), effect->state);
                        if (effect->type == 4)
                        {
                            equal("rain damage", rd8(&cpu, address + 3), effect->damage);
                            equal("rain impact X", rd16(&cpu, address + 0x1A), (uint16_t)effect->goalX);
                            equal("rain impact Y", rd16(&cpu, address + 0x1C), (uint16_t)effect->goalY);
                            equal("rain strikes", rd8(&cpu, address + 0x18), effect->step);
                            equal("rain source", rd32(&cpu, address + 0x22), reference(effect->source));
                        }
                        // one dispatch pass, record by record (0x2ECFC)
                        invoke(0x10473, address + 6, 0, 0);
                        if (cpu.r[EAX])
                            invoke(rd32(&cpu, 0x511F4 + rd8(&cpu, address + 2) * 4), address, 0, 0);
                        equal("rain tick error", 0, wor_effectTick(&runtime, effect) == WOR_ERROR);
                        equal("random state", rd32(&cpu, 0x50D7C), seed);
                    }
                    if (!any) break;
                }
                for (unsigned i = 0; i < WOR_EFFECT_COUNT; i++)
                    equal("rain released", !(rd8(&cpu, EFFECTS + i * 0x26 + 1) & 1), effects[i].used);
            }
    equal("rain terrain strikes", 1, rainTerrainStrikes > 0);
    runtime.effects = NULL;
    runtime.context = NULL;
    runtime.occupant = NULL;
    runtime.damage = NULL;
    runtime.terrainDamage = NULL;
    return rainCases;
}

int main(int argc, char** argv)
{
    if (argc != 2) { fprintf(stderr, "usage: test-order-kernel REFERENCE_IMAGE\n"); return 2; }
    cpu.mem = calloc(1, MEMORY_SIZE);
    if (!cpu.mem) { perror("guest memory"); return 2; }
    cpu.mem_size = MEMORY_SIZE;
    cpu.hooks = &hooks;
    FILE* image = fopen(argv[1], "rb");
    if (!image) { perror(argv[1]); free(cpu.mem); return 2; }
    uint32_t header[7];
    if (fread(header, sizeof(header), 1, image) != 1 || header[0] != 0x314D4957 ||
        header[3] >= MEMORY_SIZE || header[4] > MEMORY_SIZE - header[3] ||
        fread(cpu.mem + header[3], 1, header[4], image) != header[4])
    {
        fprintf(stderr, "invalid reference image: %s\n", argv[1]);
        fclose(image); free(cpu.mem); return 2;
    }
    fclose(image);
    wor_clearUnits(units);
    for (unsigned type = 0; type <= 63; type++)
    {
        scripts[type * 2] = (uint8_t)(128 | (type & 1));
        scripts[type * 2 + 1] = 0;
    }
    for (unsigned animation = 0; animation < 6; animation++)
    {
        const uint16_t start = (uint16_t)(192 + animation * 16);
        scripts[128 + animation * 2] = (uint8_t)start;
        scripts[129 + animation * 2] = (uint8_t)(start >> 8);
    }
    memcpy(cpu.mem + SCRIPTS, scripts, sizeof(scripts));
    wr32(&cpu, 0x531C8, SCRIPTS);
    transitions();
    const uint32_t transitionCases = cases;
    deathTransitions();
    const uint32_t deathCases = cases - transitionCases;
    exploration();
    const uint32_t explorationCases = cases - transitionCases - deathCases;
    attackZones();
    const uint32_t beforeLines = cases;
    linePaths();
    const uint32_t lineCases = cases - beforeLines;
    nearbyTrees();
    replacementPosts();
    terrainDamage();
    invisibility();
    manaRecovery();
    summonDecay();
    exitSearch();
    summoning();
    catapultSafety();
    shotPayment();
    const uint32_t beforeHealing = cases;
    healingAndArmour();
    const uint32_t healingCases = cases - beforeHealing;
    protectionTimers();
    facing();
    resurrection();
    wind();
    areaTargets();
    poison();
    const unsigned rainCases = rain();
    scriptControl();
    printf("matched original: %u transition/target cases, %u death/animation cases, "
           "%u exploration/visibility cases, 200 attack-zone cases, %u line paths, 200 tree searches, "
           "400 replacement-post cases, 512 terrain-damage cases, 512 invisibility cases, 3072 mana cases, "
           "512 exit searches, 2048 summoning cases, 624 decay cases, 512 catapult safety cases, "
           "3840 shot-payment cases, %u healing/armour cases, 216 protection-timer cases, 16129 tile facings, "
           "2048 resurrection selection/payload cases, 20000 wind ticks, 1024 area targets, 1536 poison cases, "
           "%u rain cases and %u script ticks\n",
           transitionCases, deathCases, explorationCases, lineCases, healingCases, rainCases, scriptTicks);
    free(cpu.mem);
    return 0;
}
