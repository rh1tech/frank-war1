// frank-war1 — the original's walking, in place of war1's flow field and
// avoidance (included by war_state_machine_move.c).
//
// WarCraft 1.20 moves a unit one tile at a time, one unit to a tile
// (docs/ai.md "Walking"). Every game tick (30 ms at normal speed) it runs
// the unit's walk script (DATA.WAR's unit scripts, port/war_scripts_frank.c):
// at the script's checkpoint the move order (WAR.EXE 0x223D8) checks for
// arrival, plans a route when it has none, and takes the route's next tile
// at once: the unit stands on the new tile from the start of the step,
// while the script slides its picture there 1 pixel at a time.
//   - arrival: within the order's range of the goal; a building as goal is
//     its nearest edge tile, or the middle of a side (0x221D8)
//   - route (0x2CD44): a straight line to the goal; when something stands
//     in the way, a walk along the obstacle's edge both ways round, the
//     shorter kept, straightened (0x2C880); at most 20 steps, then again
//   - a unit on the next tile: wait 40 ticks; keep the route while it is a
//     friend walking away (the first 7 times), else plan again; give up
//     the 15th time (0x22080)
// A unit that is walking counts as no obstacle when routes are planned
// (0x21F48); an attack-move plans through enemies.
// The tile a unit stands on is the one it walks onto (war1's tile queries
// answer it, wu_getUnitTile), and other orders wait for the step to end, as
// the original takes new orders only at the checkpoint.
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib

#define WMV_W           MAP_TILES_WIDTH
#define WMV_TILES       (MAP_TILES_WIDTH * MAP_TILES_HEIGHT)
#define WMV_PATH        20
#define WMV_NONE        0xFE
#define WMV_TICK        (1.0 / 33.333)
#define WMV_BLOCK_WAIT  40
#define WMV_MARK_START  0x20
#define WMV_MARK_NEAR   0x40
#define WMV_MARK_BEYOND 0x80

#ifdef FRANK_BOARD
#define WMV_BSS __attribute__((section(".psram_bss")))
#else
#define WMV_BSS
#endif

extern bool wscr_walkScript(WarUnitType type, const u8** data, u32* length, u32* start);

static const s8 wmv_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const s8 wmv_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
// a line step (dx, dy) as a direction
static const u8 wmv_lineDir[9] = { 7, 0, 1, 6, 0, 2, 5, 4, 3 };
// two steps that make one (the second step's direction, by the first's),
// or > 7: none (WAR.EXE 0x55164)
static const u8 wmv_join[8][8] = {
    { 255, 255,   1,   2, 253,   6,   7, 255 },
    { 255, 255, 255, 254,   2, 253,   0, 254 },
    {   1, 255, 255, 255,   3,   4, 253,   0 },
    {   2, 254, 255, 255, 255, 254,   4, 253 },
    { 253,   2,   3, 255, 255, 255,   5,   6 },
    {   6, 253,   4, 254, 255, 255, 255, 254 },
    {   7,   0, 253,   4,   5, 255, 255, 255 },
    { 255, 254,   0, 253,   6, 254, 255, 255 },
};

static u16 wmv_terrain[WMV_TILES] WMV_BSS;   // the level's passability (64 water, 128 trees)
static u16 wmv_visual[WMV_TILES] WMV_BSS;
static WarOriginalTerrain wmv_terrainState;
static bool wmv_visualReady;
static u8 wmv_built[WMV_TILES] WMV_BSS;      // 1: a building, a wall (this tick)
// 0x55104: a wall's artwork row by its walled neighbours (8 below, 4 right,
// 2 left, 1 above)
static const u8 wmv_wallShape[16] = { 2, 2, 9, 3, 9, 1, 9, 5, 12, 12, 11, 6, 8, 4, 10, 7 };
static u16 wmv_set[2 * WMV_TILES] WMV_BSS;   // the tiles set this tick, to clear (built, then units)
static s32 wmv_setBuilt, wmv_setUnits;
static u16 wmv_unitAt[WMV_TILES] WMV_BSS;    // the entity pool slot + 1 of the unit on a tile
static u8 wmv_mark[WMV_TILES] WMV_BSS;       // route planning marks
static u16 wmv_dir[WMV_TILES] WMV_BSS;       // route planning: the way on, line (high byte), route (low)
static bool wmv_haveTerrain;
static u8 wmv_ground[WMV_TILES] WMV_BSS;     // water and rocks
static u16 wmv_trees[WMV_TILES] WMV_BSS;     // the level's tree tiles
static s32 wmv_treeCount;
static u8 wmv_treeUp[WMV_TILES] WMV_BSS;     // a tree stands there (until chopped down)
// water, rocks, standing trees, buildings and walls
#define WMV_STATIC(i) (wmv_ground[i] | wmv_treeUp[i] | wmv_built[i] | (wmv_terrain[i] & 0xC))
static WarMap* wmv_map;
static f64 wmv_nextTick;

void wmv_setTerrain(const u16* data)
{
    memcpy(wmv_terrain, data, sizeof(wmv_terrain));
    wmv_haveTerrain = true;
    wmv_visualReady = false;
    wmv_treeCount = 0;
    memset(wmv_built, 0, sizeof(wmv_built));
    memset(wmv_unitAt, 0, sizeof(wmv_unitAt));
    wmv_setBuilt = wmv_setUnits = 0;
    for (s32 i = 0; i < WMV_TILES; i++)
    {
        wmv_ground[i] = (data[i] & 0x240) != 0;
        wmv_treeUp[i] = (data[i] & 0x80) != 0;
        if (data[i] & 0x80) wmv_trees[wmv_treeCount++] = (u16)i;
    }
#ifndef FRANK_BOARD
    if (getenv("WAR_TERRAINDUMP"))
    {
        s32 hist[16] = {0};
        for (s32 i = 0; i < WMV_TILES; i++) for (s32 b = 0; b < 16; b++) if (data[i] & (1 << b)) hist[b]++;
        for (s32 b = 0; b < 16; b++) if (hist[b]) fprintf(stderr, "terrain bit %04x: %d\n", 1 << b, hist[b]);
    }
#endif
    wmv_map = NULL;
}

static inline s32 wmv_cheb(s32 ax, s32 ay, s32 bx, s32 by)
{
    return MAX(ABS(ax - bx), ABS(ay - by));
}

static inline bool wmv_inside(s32 x, s32 y)
{
    return (u32)x < MAP_TILES_WIDTH && (u32)y < MAP_TILES_HEIGHT;
}

bool wmv_hasTerrain(s32 x, s32 y, u16 mask)
{
    return wmv_inside(x, y) && (wmv_terrain[y * WMV_W + x] & mask) != 0;
}

static void wmv_terrainError(WarOriginalRuntime* runtime, u16 slot, const char* message)
{
    NOT_USED(runtime); NOT_USED(slot);
    logError("original kernel: %s", message);
}

// The level's artwork and the wall/transition table of its tileset (the
// original loads the same resource at 0x2BA0C, rows after a 4-byte header).
static bool wmv_visualInit(WarContext* context)
{
    if (!wmv_visualReady)
    {
        WarResource* level = wres_getOrCreateResource(context, context->map->levelInfoIndex);
        WarResource* visual = wres_getOrCreateResource(context, level->levelInfo.visualIndex);
        const s32 tableIndex = level->levelInfo.tilesIndex == 190 ? 199
            : level->levelInfo.tilesIndex == 193 ? 203 : 207;
        if (!wfile_readResource(context->warFile, tableIndex))
        {
            logError("original kernel: terrain transition resource %d unavailable", tableIndex);
            return false;
        }
        WarRawResource* table = &context->warFile->resources[tableIndex];
        memcpy(wmv_visual, visual->levelVisual.data, sizeof(wmv_visual));
        wmv_terrainState = (WarOriginalTerrain){
            wmv_terrain, wmv_visual, table->data, table->length, true
        };
        wmv_visualReady = true;
    }
    return true;
}

static WarWallPiece* wmv_wallPieceAt(WarContext* context, s32 x, s32 y)
{
    WarEntityList* walls = we_getEntitiesOfType(context, WAR_ENTITY_TYPE_WALL);
    for (s32 i = 0; i < walls->count; i++)
    {
        WarWallComponent* wall = walls->items[i] ? we_getWallComponent(context, walls->items[i]) : NULL;
        if (!wall) continue;
        for (s32 k = 0; k < wall->pieces.count; k++)
            if (wall->pieces.items[k].tilex == x && wall->pieces.items[k].tiley == y)
                return &wall->pieces.items[k];
    }
    return NULL;
}

// 0x2C054: give a wall tile the artwork for its walled neighbours; with
// `bits`, first lay that owner's wall bit in place of the low nibble
static void wmv_fitWall(WarContext* context, s32 x, s32 y, u16 bits)
{
    if (!wmv_inside(x, y)) return;
    const s32 t = y * WMV_W + x;
    if (!((wmv_terrain[t] | bits) & 0xC)) return;
    if (bits) wmv_terrain[t] = (u16)((wmv_terrain[t] & ~0xF) | bits);
    if (!wmv_visualReady) return;
    const u16 shape = wmv_wallShape[(wmv_hasTerrain(x, y + 1, 0xC) << 3) | (wmv_hasTerrain(x + 1, y, 0xC) << 2)
                                    | (wmv_hasTerrain(x - 1, y, 0xC) << 1) | wmv_hasTerrain(x, y - 1, 0xC)];
    const u8* table = wmv_terrainState.wallTable;
    const u32 offset = 4 + (u32)shape * (u32)(table[0] | table[1] << 8) * 2;   // (column 0: a finished wall)
    if (offset + 2 > wmv_terrainState.wallTableLength)
    {
        logError("original kernel: truncated wall artwork table");
        return;
    }
    const u16 art = (u16)(((table[offset] | table[offset + 1] << 8) & 0xFFF) + 9);
    if ((wmv_visual[t] & 0x7FF) == art) return;
    wmv_visual[t] = (u16)((wmv_visual[t] & 0xF800) | art);
    wmap_setMapTileIndex(context, x, y, art);
}

// 0x2C23C: a wall piece laid on the map: its owner's bit (0x51DC0: 4 for the
// player, 8 for the computers), the artwork round it, durability 20
static void wmv_layWall(WarContext* context, WarWallPiece* piece)
{
    piece->frankLaid = true;
    const s32 x = piece->tilex, y = piece->tiley;
    const u16 bit = piece->player == 0 ? 4 : piece->player < 4 ? 8 : 0;
    if (!wmv_inside(x, y)) return;
    wmv_visualInit(context);
    wmv_fitWall(context, x, y, bit);
    wmv_fitWall(context, x - 1, y, 0);
    wmv_fitWall(context, x + 1, y, 0);
    wmv_fitWall(context, x, y - 1, 0);
    wmv_fitWall(context, x, y + 1, 0);
    wmv_visual[y * WMV_W + x] = (u16)((wmv_visual[y * WMV_W + x] & 0x7FF) | (20 << 11));
}

// 0x105D0: a strike on a wall or the terrain wears it down; worn out, it
// takes the next artwork, and the last leaves no wall bit
void wmv_damageTerrain(WarContext* context, s32 x, s32 y, u8 damage)
{
    if (!wmv_hasTerrain(x, y, 0xC)) return;
    if (!wmv_visualInit(context)) return;
    u16 before[3];
    for (s32 i = 0; i < 3; i++)
        before[i] = wmv_inside(x, y + i - 1) ? wmv_visual[(y + i - 1) * WMV_W + x] : 0;
    WarOriginalRuntime runtime = { .error = wmv_terrainError };
    if (wor_damageTerrain(&runtime, &wmv_terrainState, x, y, damage) == WOR_ERROR) return;
#ifndef FRANK_BOARD
    if (getenv("WAR_TERRAINDEBUG"))
        fprintf(stderr, "terrain tick %u at %d,%d damage %u visual %04x -> %04x flags %03x\n",
                wrng_tick, x, y, damage, before[1], wmv_visual[y * WMV_W + x], wmv_terrain[y * WMV_W + x]);
#endif
    for (s32 i = 0; i < 3; i++)
    {
        const s32 ty = y + i - 1;
        if (!wmv_inside(x, ty)) continue;
        const s32 tile = ty * WMV_W + x;
        if ((before[i] & 0x7FF) == (wmv_visual[tile] & 0x7FF)) continue;
        wmap_setMapTileIndex(context, x, ty, wmv_visual[tile] & 0x7FF);
        if (!WMV_STATIC(tile)) wpath_setFreeTiles(&context->map->finder, x, ty, 1, 1);
        // a war1 wall piece goes with its wall bit
        WarWallPiece* piece = wmv_wallPieceAt(context, x, ty);
        if (piece && piece->frankLaid && !(wmv_terrain[tile] & 0xC)) piece->hp = 0;
    }
}

// a unit stands where it walks to; otherwise where its centre is
static void wmv_unitTile(WarContext* context, WarEntity* e, WarUnitComponent* u, s32* x, s32* y)
{
    if (u->wmvStepping) { *x = u->wmvTileX; *y = u->wmvTileY; return; }
    vec2 c = wu_getUnitCenterPosition(context, e);
    *x = (s32)(c.x / MEGA_TILE_WIDTH);
    *y = (s32)(c.y / MEGA_TILE_HEIGHT);
}

// 0x13928: a unit put on the map (out of a building, made or summoned)
// holds its tile at once, so the units after it in the same tick find it
void wmv_occupy(WarContext* context, WarEntity* entity)
{
    WarUnitComponent* u = we_getUnitComponent(context, entity);
    if (!u) return;
    s32 x, y;
    wmv_unitTile(context, entity, u, &x, &y);
    if (!wmv_inside(x, y)) return;
    const s32 t = y * WMV_W + x;
    if (!wmv_unitAt[t] && wmv_setUnits < WMV_TILES) wmv_set[WMV_TILES + wmv_setUnits++] = (u16)t;
    wmv_unitAt[t] = (u16)(entity - we_getEntityManager(context)->entities + 1);
}

static bool wmv_onMap(WarContext* context, WarEntity* e, WarUnitComponent* u)
{
    if (u->hp <= 0 || !we_isComponentEnabled(context, e, COMP_SPRITE)) return false;
    WarStateBase* st = wst_getActiveState(context, e);
    return !st || st->type != WAR_STATE_DEATH;
}

// The obstacles of the tick: the level's water and standing trees, the
// buildings and walls, and a unit on each tile.
static void wmv_buildGrids(WarContext* context)
{
    // (cleared where set last tick: a full clear each tick cost the board)
    for (s32 k = 0; k < wmv_setBuilt; k++) wmv_built[wmv_set[k]] = 0;
    for (s32 k = 0; k < wmv_setUnits; k++) wmv_unitAt[wmv_set[WMV_TILES + k]] = 0;
    wmv_setBuilt = wmv_setUnits = 0;
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 i = 1; i < MAX_ENTITIES_COUNT; i++)
    {
        WarEntity* e = &manager->entities[i];
        if (!e->id) continue;
        if (e->type == WAR_ENTITY_TYPE_WALL)
        {
            WarWallComponent* wall = we_getWallComponent(context, e);
            if (!wall) continue;
            for (s32 k = 0; k < wall->pieces.count; k++)
            {
                WarWallPiece* p = &wall->pieces.items[k];
                // a finished piece is laid in the terrain, which is its
                // obstacle and wears it down from then on (0x2C23C)
                if (p->hp > 0 && !p->frankLaid) wmv_layWall(context, p);
            }
            continue;
        }
        if (e->type != WAR_ENTITY_TYPE_UNIT) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || u->hp <= 0 || wu_isCorpseUnitType(u->type)) continue;   // (the dead and the collapsing have none left)
        if (wu_isBuildingUnitType(u->type))
        {
            for (s32 y = u->tiley; y < u->tiley + u->sizey; y++)
                for (s32 x = u->tilex; x < u->tilex + u->sizex; x++)
                {
                    if (!wmv_inside(x, y)) continue;
                    const s32 t = y * WMV_W + x;
                    if (!wmv_built[t]) { wmv_built[t] = 1; wmv_set[wmv_setBuilt++] = (u16)t; }
                }
            continue;
        }
        if (!we_isComponentEnabled(context, e, COMP_SPRITE)) continue;
        s32 x, y;
        wmv_unitTile(context, e, u, &x, &y);
        if (!wmv_inside(x, y)) continue;
        const s32 t = y * WMV_W + x;
        if (!wmv_unitAt[t]) wmv_set[WMV_TILES + wmv_setUnits++] = (u16)t;
        wmv_unitAt[t] = (u16)(i + 1);
    }
}

static WarStateMove* wmv_activeMove(WarContext* context, WarEntity* e)
{
    WarStateBase* s = wst_getActiveState(context, e);
    return s && s->type == WAR_STATE_MOVE ? (WarStateMove*)s : NULL;
}

// 0x21F48: a unit walking (and not held up) is no obstacle to a route
static bool wmv_walkingOn(WarContext* context, WarEntity* o)
{
    WarUnitComponent* u = we_getUnitComponent(context, o);
    const bool native = u && wmv_activeMove(context, o) && u->wmvBlocks == 0;
#ifndef FRANK_BOARD
    WarOriginalUnit* r = u ? wos_unit(context, o) : NULL;
    if (r && getenv("WAR_WALKDEBUG"))
    {
        bool orig;
        if (r->flags & WOR_INACTIVE) orig = r->type <= 31;
        else orig = (r->current == 3 || r->current == 12) && !u->wmvBlocks && !(r->flags & WOR_HELD);
        if (orig != native)
            fprintf(stderr, "walkdiff tick %u id %u type %d current %u flags %04x blocks %d native %d\n",
                    wrng_tick, (unsigned)o->id, u->type, r->current, r->flags, u->wmvBlocks, native);
    }
#endif
    return native;
}

typedef struct
{
    WarContext* context;
    WarEntity* self;
    bool throughEnemies;
    u16 wallMask;
    s32 sx, sy, gx, gy;
    s32 lastX, lastY;   // the line's last free tile
    u8 lastDir;         // and the step onto it
    s32 count;          // free steps on the line
} WarMovePlan;

static bool wmv_blockedForRoute(WarMovePlan* p, s32 x, s32 y)
{
    s32 i = y * WMV_W + x;
    if (wmv_ground[i] || wmv_treeUp[i] || wmv_built[i] || (wmv_terrain[i] & p->wallMask)) return true;
    u16 slot = wmv_unitAt[i];
    if (!slot) return false;
    WarEntity* o = &we_getEntityManager(p->context)->entities[slot - 1];
    if (p->throughEnemies && o != p->self && wu_areEnemies(p->context, p->self, o)) return false;
    return !wmv_walkingOn(p->context, o);
}

// 0x2CA48: the straight line to the goal; true when it is free (up to 20
// steps, or to the goal even with someone on it).
static bool wmv_trace(WarMovePlan* p)
{
    WarOriginalLine l;
    wor_lineInit(&l, p->sx, p->sy, p->gx, p->gy);
    bool free = true;
    p->lastX = p->sx; p->lastY = p->sy; p->lastDir = WMV_NONE; p->count = 0;
    s32 x = p->sx, y = p->sy;
    wmv_mark[y * WMV_W + x] |= WMV_MARK_START;
    for (s32 guard = 0; guard < 256; guard++)
    {
        s32 ax, ay;
        u8 d = wor_lineStep(&l, &ax, &ay);
        if (p->lastDir == WMV_NONE) p->lastDir = d;
        wmv_dir[y * WMV_W + x] = (u16)(d | d << 8);
        x += ax; y += ay;
        if (!wmv_inside(x, y)) return false;
        if (free)
        {
            if (wmv_blockedForRoute(p, x, y))
            {
                if (x != p->gx || y != p->gy) free = false;
            }
            else
            {
                p->lastX = x; p->lastY = y; p->lastDir = d;
                if (++p->count >= WMV_PATH) return true;
            }
        }
        if (!free) wmv_mark[y * WMV_W + x] |= WMV_MARK_BEYOND;
        if (x == p->gx && y == p->gy) break;
    }
    return free;
}

// 0x2CC1C: the goal's surroundings (a target's footprint and a tile round it)
static void wmv_markNear(s32 x0, s32 y0, s32 w, s32 h)
{
    for (s32 y = y0; y < y0 + h; y++)
        for (s32 x = x0; x < x0 + w; x++)
            if (wmv_inside(x, y)) wmv_mark[y * WMV_W + x] |= WMV_MARK_NEAR | WMV_MARK_BEYOND;
}

// 0x2C66C: along the obstacle from the line's last free tile, keeping it on
// the `turn` side (-1 or 1), until back on the line past the obstacle, or
// next to the goal; `*ox, *oy` the end when not the goal.
static bool wmv_follow(WarMovePlan* p, s32 turn, s32* ox, s32* oy)
{
    s32 x = p->lastX, y = p->lastY;
    u8 d = p->lastDir;
    for (s32 steps = 1; steps < 100; steps++)
    {
        d = (u8)((d + turn) & 7);
        const u8 first = d;
        s32 nx, ny;
        for (;;)
        {
            nx = x + wmv_dx[d];
            ny = y + wmv_dy[d];
            if (!wmv_inside(nx, ny)) return false;
            if (!wmv_blockedForRoute(p, nx, ny)) break;
            d = (u8)((d + turn) & 7);
            if (d == first) return false;
        }
        const u8 m = wmv_mark[ny * WMV_W + nx];
        if (m & WMV_MARK_START) return false;
        wmv_dir[y * WMV_W + x] = (u16)((wmv_dir[y * WMV_W + x] & 0xFF00) | d);
        if (m & WMV_MARK_BEYOND)
        {
            if (m & WMV_MARK_NEAR) { *ox = nx; *oy = ny; }
            return true;
        }
        const u8 side = (u8)((d + turn) & 7);
        const s32 mx = x + wmv_dx[side], my = y + wmv_dy[side];   // (from where it stands)
        if (wmv_inside(mx, my) && (wmv_mark[my * WMV_W + mx] & WMV_MARK_BEYOND)) { *ox = nx; *oy = ny; return true; }
        x = nx; y = ny;
        d = (u8)((d - 3 * turn) & 7);
    }
    *ox = x; *oy = y;
    return true;
}

// 0x2C880: the route from the start to (ox, oy), two steps made one where
// they can be, again until nothing changes; its length.
static s32 wmv_straighten(WarMovePlan* p, s32 ox, s32 oy)
{
    for (s32 pass = 0; pass < 64; pass++)
    {
        bool changed = false;
        s32 steps = 0, x = p->sx, y = p->sy;
        for (s32 guard = 0; (x != ox || y != oy) && guard < WMV_TILES; guard++)
        {
            steps++;
            const u8 d1 = (u8)(wmv_dir[y * WMV_W + x] & 0xFF);
            if (d1 > 7) return 0x7FFF;
            const s32 x2 = x + wmv_dx[d1], y2 = y + wmv_dy[d1];
            if (x2 == ox && y2 == oy) break;
            if (!wmv_inside(x2, y2)) return 0x7FFF;
            const u8 d2 = (u8)(wmv_dir[y2 * WMV_W + x2] & 0xFF);
            if (d2 > 7) return 0x7FFF;
            const u8 c = wmv_join[d1][d2];
            if (c <= 7)
            {
                wmv_dir[y * WMV_W + x] = (u16)((wmv_dir[y * WMV_W + x] & 0xFF00) | c);
                changed = true;
                x = x2 + wmv_dx[d2];
                y = y2 + wmv_dy[d2];
            }
            else
            {
                x = x2;
                y = y2;
            }
        }
        if (!changed) return steps;
    }
    return 0x7FFF;
}

// 0x2C9B0: up to `n` steps of the planned way (the line's or the route's)
// into the unit's route, ending at (ex, ey)
static void wmv_writePath(WarMovePlan* p, WarUnitComponent* u, s32 n, bool line, s32 ex, s32 ey)
{
    s32 x = p->sx, y = p->sy, i = 0;
    while (i < WMV_PATH && n-- > 0)
    {
        const u16 v = wmv_dir[y * WMV_W + x];
        const u8 d = (u8)(line ? v >> 8 : v & 0xFF);
        if (d > 7) break;
        u->wmvPath[i++] = d;
        x += wmv_dx[d];
        y += wmv_dy[d];
        if (x == ex && y == ey) break;
    }
    while (i < WMV_PATH) u->wmvPath[i++] = WMV_NONE;
}

// 0x2CD44: a new route to (gx, gy); false when there is no first step
static bool wmv_plan(WarContext* context, WarEntity* e, WarUnitComponent* u, WarStateMove* mv,
                     s32 gx, s32 gy, WarEntity* target)
{
    WarMovePlan p = {
        .context = context, .self = e, .wallMask = 0xC,
        .sx = u->wmvTileX, .sy = u->wmvTileY, .gx = gx, .gy = gy, .lastDir = WMV_NONE
    };
    u->wmvPathIndex = 0;
    const s32 dist0 = wmv_cheb(p.sx, p.sy, gx, gy);
    p.throughEnemies = mv->frankNextOrder == 6 && !wai_hasRetainedAttacker(context, e);
    WarResource* level = wres_getOrCreateResource(context, context->map->levelInfoIndex);
    if (level->levelInfo.ai && level->levelInfo.ai->alliance[u->player] != 0 &&
        !wu_isWorkerUnitType(u->type) && !wai_hasRetainedAttacker(context, e))
    {
        if (level->levelInfo.tilesIndex == 196) p.wallMask = 0;
        else if (!u->invisible) p.wallMask &= (u16)~(u->player == 0 ? 8 : u->player < 4 ? 4 : 0);
    }

    if (target)
    {
        WarUnitComponent* tu = we_getUnitComponent(context, target);
        s32 tx = gx, ty = gy, tw = 1, th = 1;
        if (tu && wu_isBuildingUnitType(tu->type)) { tx = tu->tilex; ty = tu->tiley; tw = tu->sizex; th = tu->sizey; }
        else if (tu) wmv_unitTile(context, target, tu, &tx, &ty);
        wmv_markNear(tx - 1, ty - 1, tw + 2, th + 2);
    }
    else
    {
        wmv_markNear(gx - 1, gy - 1, 3, 3);
    }

#ifndef FRANK_BOARD
    static s32 debugId = -2;
    if (debugId == -2) debugId = getenv("WAR_MOVEDEBUG") ? atoi(getenv("WAR_MOVEDEBUG")) : -1;
    const bool debug = debugId == (s32)e->id;
#define WMV_DEBUG(...) do { if (debug) fprintf(stderr, __VA_ARGS__); } while (0)
#else
    const bool debug = false;
#define WMV_DEBUG(...) do { } while (0)
#endif
    WMV_DEBUG("plan %d,%d -> %d,%d target %d\n", p.sx, p.sy, gx, gy, target ? (s32)target->id : 0);
#ifndef FRANK_BOARD
    if (debug)
    {
        for (s32 y = MAX(p.sy - 3, 0); y <= MIN(p.sy + 3, MAP_TILES_HEIGHT - 1); y++)
        {
            fprintf(stderr, "   %2d:", y);
            for (s32 x = MAX(p.sx - 3, 0); x <= MIN(p.sx + 3, MAP_TILES_WIDTH - 1); x++)
            {
                const s32 i = y * WMV_W + x;
                fprintf(stderr, " %c%d/%03x/%04x", WMV_STATIC(i) ? (wmv_terrain[i] & 0x80 ? 'T' : '#') : '.', wmv_unitAt[i] ? (s32)we_getEntityManager(context)->entities[wmv_unitAt[i] - 1].id : 0, wmv_terrain[i], wmap_getMapTileIndex(context, x, y));
            }
            fprintf(stderr, "   (x %d..)\n", MAX(p.sx - 3, 0));
        }
    }
#endif
    if (wmv_trace(&p) || p.count >= WMV_PATH)
    {
        WMV_DEBUG("  line free, %d steps\n", p.count);
        wmv_writePath(&p, u, p.count, true, gx, gy);
    }
    else
    {
        bool found = false;
        s32 best = 0x7FFF, ox = gx, oy = gy;
        WMV_DEBUG("  line blocked after %d, last free %d,%d dir %d\n", p.count, p.lastX, p.lastY, p.lastDir);
        bool f1 = wmv_follow(&p, -1, &ox, &oy);
        WMV_DEBUG("  follow -1: %d end %d,%d\n", f1, ox, oy);
        if (f1 && dist0 + 8 >= wmv_cheb(ox, oy, gx, gy))
        {
            const s32 n = wmv_straighten(&p, ox, oy);
            WMV_DEBUG("    %d steps\n", n);
            if (n < 0x7FFF) { best = n; wmv_writePath(&p, u, n, false, ox, oy); found = true; }
        }
        if (best > 4)
        {
            wmv_trace(&p);
            ox = gx; oy = gy;
            bool f2 = wmv_follow(&p, 1, &ox, &oy);
            WMV_DEBUG("  follow +1: %d end %d,%d\n", f2, ox, oy);
            if (f2 && dist0 + 8 >= wmv_cheb(ox, oy, gx, gy))
            {
                const s32 n = wmv_straighten(&p, ox, oy);
                WMV_DEBUG("    %d steps\n", n);
                if (n < best) { wmv_writePath(&p, u, n, false, ox, oy); found = true; }
            }
        }
        if (!found)
        {
            wmv_trace(&p);
            wmv_writePath(&p, u, p.count, true, gx, gy);
        }
    }
    if (debug) { WMV_DEBUG("  path"); for (s32 i = 0; i < WMV_PATH; i++) WMV_DEBUG(" %d", u->wmvPath[i]); WMV_DEBUG("\n"); }
    memset(wmv_mark, 0, sizeof(wmv_mark));
    return u->wmvPath[0] != WMV_NONE;
}

// 0x21FB0: the unit on (x, y) is a friend walking off it (not towards us)
static bool wmv_friendWalksOff(WarContext* context, WarEntity* e, WarUnitComponent* u, s32 x, s32 y)
{
    u16 slot = wmv_unitAt[y * WMV_W + x];
    if (!slot) return false;
    WarEntity* o = &we_getEntityManager(context)->entities[slot - 1];
    if (wu_areEnemies(context, e, o) || !wmv_walkingOn(context, o)) return false;
    WarUnitComponent* ou = we_getUnitComponent(context, o);
    if (ou->wmvPathIndex >= WMV_PATH || ou->wmvPath[ou->wmvPathIndex] == WMV_NONE) return false;
    const u8 d = ou->wmvPath[ou->wmvPathIndex];
    return x + wmv_dx[d] != u->wmvTileX || y + wmv_dy[d] != u->wmvTileY;
}

// 0x221D8: a building as goal: its tile nearest along each axis, the middle
// one when the unit is level with it
static void wmv_goalOnBuilding(WarUnitComponent* tu, s32 ux, s32 uy, s32* gx, s32* gy)
{
    const s32 hw = tu->sizex >> 1, hh = tu->sizey >> 1;
    const s32 cx = tu->tilex + hw, cy = tu->tiley + hh;
    if (ux <= cx - hw) *gx = cx - hw;
    else if (cx + hw <= ux) *gx = cx + hw - ((tu->sizex & 1) == 0);
    else *gx = cx;
    if (uy <= cy - hh) *gy = cy - hh;
    else if (cy + hh <= uy) *gy = cy + hh - ((tu->sizey & 1) == 0);
    else *gy = cy;
}

// 0x1DF50: the direction from one tile towards another (a diagonal only when
// both ways are near even)
static u8 wmv_dirTo(s32 fx, s32 fy, s32 tx, s32 ty)
{
    s32 dx = tx - fx, dy = ty - fy, bx, by;
    if (dx >= 0) bx = 2; else { bx = 0; dx = -dx; }
    if (dy >= 0) by = 2; else { by = 0; dy = -dy; }
    if (dx >= dy) { s32 h = MAX(dx >> 1, 1); if (h > dy) by = 1; }
    else { s32 h = MAX(dy >> 1, 1); if (h > dx) bx = 1; }
    return wmv_lineDir[by * 3 + bx];
}

static void wmv_stand(WarContext* context, WarEntity* e)
{
    wact_setAction(context, e, WAR_ACTION_TYPE_IDLE, false, 1.0f);
}

// Worker constructors queue move/work; both command resolution and walking
// test the following work order's stopping range (0x22554, 0x223D8).
static void wmv_prepareWork(WarContext* context, WarEntity* entity, WarStateBase* state)
{
    WarEntity* target = NULL;
    vec2 goal;
    const u8 next = state->type == WAR_STATE_DELIVER ? 11 : 10;
    if (state->type == WAR_STATE_WOOD)
    {
        WarStateWood* wood = (WarStateWood*)state;
        vec2 requested = wmap_mapToTileCoordinatesV(wood->position);
        goal = wmv_backOff(context, entity, (s32)requested.x, (s32)requested.y);
        wood->position = wmap_tileToMapCoordinatesV(goal, true);
    }
    else
    {
        WarEntityId id = state->type == WAR_STATE_GOLD ? ((WarStateGold*)state)->goldmineId
                                                        : ((WarStateDeliver*)state)->townHallId;
        target = we_findEntity(context, id);
        WarUnitComponent* other = target && wu_isUnit(target) ? we_getUnitComponent(context, target) : NULL;
        if (!other || other->hp <= 0 || wst_isGoingToCollapse(context, target))
        {
            wst_resetState(context, entity, (WarStateBase*)wst_createIdleState(context, entity, false),
                           WAR_TRANSITION_CAUSE_COMPLETION);
            return;
        }
        goal = wmv_buildingGoal(context, entity, target);
    }
    WarOriginalUnit* original = wos_unit(context, entity);
    if (!original) return;
    original->goalX = (s16)goal.x;
    original->goalY = (s16)goal.y;
    original->target = target ? we_getUnitComponent(context, target)->frankOriginalSlot : 0;
    vec2 here = wu_getUnitCenterTile(context, entity);
    if (MAX(ABS((s32)here.x - (s32)goal.x), ABS((s32)here.y - (s32)goal.y)) <= 1)
    {
        wos_startPair(context, entity, next, WOR_NONE);
        return;
    }
    WarStateMove* move = wst_createMoveState(context, entity, 2,
        arrayArg(vec2, wu_getUnitCenterPosition(context, entity), wmap_tileToMapCoordinatesV(goal, true)), false);
    if (!move) return;
    move->frankTarget = target ? target->id : 0;
    move->frankRange = 1;
    move->frankNextOrder = next;
    wst_pushState(context, entity, (WarStateBase*)move, WAR_TRANSITION_CAUSE_COMPLETION);
}

// The original takes a unit's next order at once when one ends (0x2EE9C):
// the parent decides in the same tick (a follow in range, a worker at the
// mine, the hall or the trees), not frames later.
void wmv_settle(WarContext* context, WarEntity* e)
{
    WarStateMachineComponent* sm = we_getStateMachineComponent(context, e);
    if (!sm) return;
    for (s32 k = 0; k < 4; k++)
    {
        wst_processPendingTransitionOf(context, e);
        WarStateBase* st = wst_getActiveState(context, e);
        if (!st) return;
        const u32 seq = sm->nextTransitionSequence;
        switch (st->type)
        {
            case WAR_STATE_FOLLOW:  wst_updateFollowState(context, e, st); break;
            case WAR_STATE_ATTACK:
                if (wai_drivesTicks(context) && ((WarStateAttack*)st)->frankPrepared) return;
                wst_updateAttackState(context, e, st);
                break;
            case WAR_STATE_CAST:
                if (!wst_frankSpellOrder(((WarStateCast*)st)->spellType)) return;
                wst_updateCastState(context, e, st);
                break;
            case WAR_STATE_GOLD:
            case WAR_STATE_WOOD:
                if (wai_drivesTicks(context))
                {
                    wmv_prepareWork(context, e, st);
                    wst_processPendingTransitionOf(context, e);
                    return;
                }
                if (st->type == WAR_STATE_GOLD) wst_updateGatherGoldState(context, e, st);
                else wst_updateGatherWoodState(context, e, st);
                break;
            case WAR_STATE_DELIVER:
                if (((WarStateDeliver*)st)->insideBuilding) return;   // (its time inside first)
                if (wai_drivesTicks(context))
                {
                    wmv_prepareWork(context, e, st);
                    wst_processPendingTransitionOf(context, e);
                    return;
                }
                wst_updateDeliverState(context, e, st);
                break;
            default: return;
        }
        if (sm->nextTransitionSequence == seq) return;
    }
    wst_processPendingTransitionOf(context, e);
}

// Pending precedes the old handler and does not run the replacement
// script at the same checkpoint (0x2ECFC, 0x2EE9C).
bool wmv_takePendingOrder(WarContext* context, WarEntity* entity)
{
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    if (!unit || !unit->frankOrderQueued) return false;
    unit->frankOrderQueued = false;
    WarStateMachineComponent* sm = we_getStateMachineComponent(context, entity);
    if (!sm || sm->pending.operation == WAR_STATE_OP_NONE) return false;
    wmv_settle(context, entity);
    return true;
}

// The auto-attack fallback tries the tile directly towards the target,
// not a follow order. The first offset is zero and a blocked trial exits (0x11298).
bool wmv_attackStep(WarContext* context, WarEntity* e, WarEntity* target)
{
    WarUnitComponent* u = we_getUnitComponent(context, e);
    WarUnitComponent* tu = we_getUnitComponent(context, target);
    s32 x, y, tx, ty;
    wmv_unitTile(context, e, u, &x, &y);
    if (wu_isBuildingUnitType(tu->type)) { tx = tu->tilex; ty = tu->tiley; }
    else wmv_unitTile(context, target, tu, &tx, &ty);
    const u8 direction = wmv_dirTo(x, y, tx, ty);
    wu_setUnitDirection(context, e, (WarDirection)direction);
    tx = x + wmv_dx[direction];
    ty = y + wmv_dy[direction];
    if (!wmv_inside(tx, ty) || WMV_STATIC(ty * WMV_W + tx) || wmv_unitAt[ty * WMV_W + tx]) return false;
    WarStateMove* move = wst_createMoveState(context, e, 2,
        arrayArg(vec2, wu_getUnitCenterPosition(context, e), wmap_tileToMapCoordinatesV(vec2i(tx, ty), true)), false);
    if (!move || !wst_resetState(context, e, (WarStateBase*)move, WAR_TRANSITION_CAUSE_AI_ORDER)) return false;
    wmv_settle(context, e);
    return true;
}

static void wmv_finish(WarContext* context, WarEntity* e, WarUnitComponent* u, WarStateMove* mv, WarStateResult result)
{
    u->wmvPathIndex = WMV_PATH;
    mv->frankDone = true;
    wmv_stand(context, e);
    if (result == WAR_STATE_RESULT_SUCCESS || result == WAR_STATE_RESULT_TARGET_INVALID)
    {
        const bool entered = mv->frankEnter != 0;
        WarOriginalUnit* original = wos_unit(context, e);
        const bool work = wai_drivesTicks(context) && original &&
                          (original->next == 10 || original->next == 11);
        if (work && wos_takeNext(context, e) != WOR_CHANGED) return;
        wst_popState(context, e, WAR_TRANSITION_CAUSE_COMPLETION, result);
        if (wai_drivesTicks(context) && (entered || work))
        {
            wst_processPendingTransitionOf(context, e);
            WarStateBase* next = wst_getActiveState(context, e);
            if (next && next->type == WAR_STATE_FOLLOW)
            {
                wst_popState(context, e, WAR_TRANSITION_CAUSE_COMPLETION, result);
                wst_processPendingTransitionOf(context, e);
                next = wst_getActiveState(context, e);
            }
            if (entered && next)
            {
                wos_workHandler(context, e, next);
                wst_processPendingTransitionOf(context, e);
            }
            return;
        }
        wmv_settle(context, e);
        return;
    }
    if (result == WAR_STATE_RESULT_NO_PATH && wu_isWorkerUnitType(u->type))
    {
        // The gather follow-up permits a nearby-tree command even when
        // the failed movement was toward a mine (0x21EE0).
        WarOriginalUnit* original = wos_unit(context, e);
        s32 tx, ty;
        if (original && original->next == 10 &&
            wmv_treeAround(context, u->wmvTileX, u->wmvTileY, &tx, &ty))
        {
            WarEntity* forest = we_getForestAtTile(context, tx, ty);
            WarStateWood* wood = forest ? wst_createGatherWoodState(context, e, forest->id,
                wmap_tileToMapCoordinatesV(vec2i(tx, ty), true)) : NULL;
            if (wood && wst_resetState(context, e, (WarStateBase*)wood, WAR_TRANSITION_CAUSE_COMPLETION))
            {
                wmv_settle(context, e);
                return;
            }
            logError("original kernel: failed nearby-tree command at %d,%d", tx, ty);
        }
    }
    // the original gives up the whole order (its order 2): idle
    WarStateIdle* idle = wst_createIdleState(context, e, false);
    if (!idle || !wst_resetState(context, e, (WarStateBase*)idle, WAR_TRANSITION_CAUSE_AI_ORDER))
    {
        logError("original kernel: failed movement completion for entity %u", e->id);
        return;
    }
    // A failed route replaces the order before later units plan (0x223D8).
    if (result == WAR_STATE_RESULT_NO_PATH) wmv_settle(context, e);
}

// The move order at the walk script's checkpoint (0x223D8): true when the
// unit takes a step.
static bool wmv_checkpoint(WarContext* context, WarEntity* e, WarUnitComponent* u, WarStateMove* mv)
{
    if (u->wmvGiveUp)
    {
        u->wmvGiveUp = false;
        wmv_finish(context, e, u, mv, WAR_STATE_RESULT_BLOCKED);
        return false;
    }
    // 0x11004: on the way to attack, a look round first
    if (!mv->frankEnter)
    {
        const bool attackMove = mv->frankNextOrder == 6;
        if (wai_moveAcquire(context, e, attackMove)) return false;
    }
    if (mv->frankEnter)
    {
        // order 12 (0x1CFF4): a step onto the building, off the grid; at the
        // next checkpoint the unit is inside
        WarEntity* b = we_findEntity(context, mv->frankEnter);
        WarUnitComponent* bu = b && wu_isUnit(b) ? we_getUnitComponent(context, b) : NULL;
        if (mv->frankEntered || !bu)
        {
            wmv_finish(context, e, u, mv, bu ? WAR_STATE_RESULT_SUCCESS : WAR_STATE_RESULT_TARGET_INVALID);
            return false;
        }
        const u8 d = wmv_dirTo(u->wmvTileX, u->wmvTileY, bu->tilex, bu->tiley);
        const s32 slot = (s32)(e - we_getEntityManager(context)->entities);
        if (wmv_unitAt[u->wmvTileY * WMV_W + u->wmvTileX] == slot + 1) wmv_unitAt[u->wmvTileY * WMV_W + u->wmvTileX] = 0;
        u->wmvDir = d;
        u->wmvTileX = (s16)CLAMP(u->wmvTileX + wmv_dx[d], 0, MAP_TILES_WIDTH - 1);
        u->wmvTileY = (s16)CLAMP(u->wmvTileY + wmv_dy[d], 0, MAP_TILES_HEIGHT - 1);
        u->wmvStepping = true;
        mv->frankEntered = true;
        wai_targetsDirty = true;
        wu_setUnitDirection(context, e, (WarDirection)d);
        wact_setAction(context, e, WAR_ACTION_TYPE_WALK, true, 1.0f);
        return true;
    }
    WarEntity* target = NULL;
    if (mv->frankTarget)
    {
        target = we_findEntity(context, mv->frankTarget);
        WarUnitComponent* tu = target && wu_isUnit(target) ? we_getUnitComponent(context, target) : NULL;
        // (0x2216C: gone, inside or invisible ends the move, and the next
        // order takes over at once, 0x223D8)
        if (!tu || tu->hp <= 0 || wst_isDead(context, target) || wst_isCollapsing(context, target) ||
            !wos_moveTargetValid(context, e, target))
        {
            wmv_finish(context, e, u, mv, WAR_STATE_RESULT_TARGET_INVALID);
            return false;
        }
    }
    s32 gx, gy;
    for (;;)
    {
        if (target)
        {
            WarUnitComponent* tu = we_getUnitComponent(context, target);
            if (wu_isBuildingUnitType(tu->type)) wmv_goalOnBuilding(tu, u->wmvTileX, u->wmvTileY, &gx, &gy);
            else wmv_unitTile(context, target, tu, &gx, &gy);
            WarOriginalUnit* original = wos_unit(context, e);
            if (!original) return false;
            original->goalX = (s16)gx;
            original->goalY = (s16)gy;
        }
        else
        {
            vec2 g = mv->waypoints[mv->waypointsIndex + 1];
            gx = CLAMP((s32)(g.x / MEGA_TILE_WIDTH), 0, MAP_TILES_WIDTH - 1);
            gy = CLAMP((s32)(g.y / MEGA_TILE_HEIGHT), 0, MAP_TILES_HEIGHT - 1);
        }
        if (wmv_cheb(u->wmvTileX, u->wmvTileY, gx, gy) > mv->frankRange) break;
        if (!target && mv->waypointsIndex < mv->waypointsCount - 2)
        {
            mv->waypointsIndex++;   // on to the next waypoint
            u->wmvPathIndex = WMV_PATH;
            continue;
        }
        wmv_finish(context, e, u, mv, WAR_STATE_RESULT_SUCCESS);
        return false;
    }
    if (u->wmvPathIndex >= WMV_PATH || u->wmvPath[u->wmvPathIndex] == WMV_NONE)
    {
        if (!wmv_plan(context, e, u, mv, gx, gy, target))
        {
            wmv_finish(context, e, u, mv, WAR_STATE_RESULT_NO_PATH);
            return false;
        }
    }
    const u8 d = u->wmvPath[u->wmvPathIndex];
    const s32 nx = u->wmvTileX + wmv_dx[d], ny = u->wmvTileY + wmv_dy[d];
    if (!wmv_inside(nx, ny) || WMV_STATIC(ny * WMV_W + nx) || wmv_unitAt[ny * WMV_W + nx])
    {
        // 0x22080: held up; the walk script from its start after 40 ticks
        const u8* data; u32 length, start;
        WarOriginalUnit* original = wos_unit(context, e);
        if (!original) return false;
        if (wscr_walkScript(u->type, &data, &length, &start)) original->pc = (u16)start;
        original->wait = WMV_BLOCK_WAIT;
        wmv_stand(context, e);
        u->wmvBlocks++;
        const u16 enemyWall = u->player == 0 ? 8 : u->player < 4 ? 4 : 0;
        if (wmv_hasTerrain(nx, ny, enemyWall) && !wu_isWorkerUnitType(u->type))
        {
            if (u->invisible || wai_hasRetainedAttacker(context, e))
                u->wmvPathIndex = WMV_PATH;
            else
                wai_attackTerrain(context, e, nx, ny);
            original->wait = 1;
            return false;
        }
        if (u->wmvBlocks >= 15)
        {
            // the 15th hold-up queues idle (0x2EF9C), which the dispatcher
            // takes at this same checkpoint (0x2ECFC): idle from wait 1
            u->wmvBlocks = 0;
            wmv_finish(context, e, u, mv, WAR_STATE_RESULT_BLOCKED);
            wmv_settle(context, e);
            return false;
        }
        else if (u->wmvBlocks >= 8 || !wmv_inside(nx, ny) || !wmv_friendWalksOff(context, e, u, nx, ny))
        {
            u->wmvPathIndex = WMV_PATH;
        }
        return false;
    }
    u->wmvPathIndex++;
    u->wmvDir = d;
    const s32 slot = (s32)(e - we_getEntityManager(context)->entities);
    if (wmv_unitAt[u->wmvTileY * WMV_W + u->wmvTileX] == slot + 1) wmv_unitAt[u->wmvTileY * WMV_W + u->wmvTileX] = 0;
    u->wmvTileX = (s16)nx;
    u->wmvTileY = (s16)ny;
    if (!wmv_unitAt[ny * WMV_W + nx] && wmv_setUnits < WMV_TILES) wmv_set[WMV_TILES + wmv_setUnits++] = (u16)(ny * WMV_W + nx);
    wmv_unitAt[ny * WMV_W + nx] = (u16)(slot + 1);
    wai_targetsDirty = true;
    u->wmvStepping = true;
    wos_explore(context, e);
    wu_setUnitDirection(context, e, (WarDirection)d);
    wact_setAction(context, e, WAR_ACTION_TYPE_WALK, true, 1.0f);
    return true;
}

// One tick of a unit's walk script; the move order at its checkpoint.
static void wmv_runScript(WarContext* context, WarEntity* e, WarUnitComponent* u, WarStateMove* mv)
{
    if (wos_tick(context, e, NULL) != WOR_CHANGED) return;
    u->wmvStepping = false;
    wu_setUnitCenterTile(context, e, vec2i(u->wmvTileX, u->wmvTileY));
    if (wmv_takePendingOrder(context, e)) return;
    if (!mv || mv->frankDone) return;
    wmv_checkpoint(context, e, u, mv);
}

// The tick's obstacles (before the unit table runs).
static void wmv_tickBegin(WarContext* context)
{
    wmv_buildGrids(context);
#ifndef FRANK_BOARD
    {   // WAR_VISUALAT=x,y,w,h: the terrain and visual words of a region, once
        static bool shown;
        const char* at = getenv("WAR_VISUALAT");
        s32 rx, ry, rw, rh;
        if (at && !shown && sscanf(at, "%d,%d,%d,%d", &rx, &ry, &rw, &rh) == 4)
        {
            shown = true;
            fprintf(stderr, "wt");
            for (s32 y = ry; y < ry + rh; y++) { fprintf(stderr, " |"); for (s32 x = rx; x < rx + rw; x++) fprintf(stderr, " %04x", wmv_terrain[y * WMV_W + x]); }
            fprintf(stderr, "\nwv");
            for (s32 y = ry; y < ry + rh; y++) { fprintf(stderr, " |"); for (s32 x = rx; x < rx + rw; x++) fprintf(stderr, " %04x", wmv_visual[y * WMV_W + x]); }
            fprintf(stderr, "\n");
        }
    }
#endif
}

// One unit's walk this tick, in its turn of the unit table.
static void wmv_unitTick(WarContext* context, WarEntity* e, WarStateBase* st)
{
    if (!e->id || !wu_isUnit(e)) return;
    WarUnitComponent* u = we_getUnitComponent(context, e);
    if (!u || wu_isBuildingUnitType(u->type)) return;
    WarStateMove* mv = st && st->type == WAR_STATE_MOVE ? (WarStateMove*)st : NULL;
    if (!mv && !u->wmvStepping) return;
    if (!wmv_onMap(context, e, u))
    {
        u->wmvStepping = false;
        return;
    }
    wmv_runScript(context, e, u, mv);
}

static void wmv_tick(WarContext* context)
{
    wmv_tickBegin(context);
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 i = 1; i < MAX_ENTITIES_COUNT; i++)
    {
        WarEntity* e = &manager->entities[i];
        if (e->id && wu_isUnit(e)) wmv_unitTick(context, e, wst_getActiveState(context, e));
    }
}

// A move begins: the original's order setter drops the old route and the
// count of hold-ups (0x22554).
static void wmv_enter(WarContext* context, WarEntity* entity, WarStateMove* s)
{
    WarUnitComponent* u = we_getUnitComponent(context, entity);
    if (!u) return;
    // Entry replaces the order directly; unlike a new movement command,
    // it preserves the blockage count used by other units' planners.
    if (!s->frankEnter)
    {
        u->wmvPathIndex = WMV_PATH;
        u->wmvBlocks = 0;
        u->wmvGiveUp = false;
    }
    s->frankDone = false;
    if (!u->wmvStepping)
    {
        vec2 c = wu_getUnitCenterPosition(context, entity);
        u->wmvTileX = (s16)CLAMP((s32)(c.x / MEGA_TILE_WIDTH), 0, MAP_TILES_WIDTH - 1);
        u->wmvTileY = (s16)CLAMP((s32)(c.y / MEGA_TILE_HEIGHT), 0, MAP_TILES_HEIGHT - 1);
        if (wai_drivesTicks(context) && (s->frankNextOrder == 7 ||
            s->frankNextOrder == 17 || s->frankNextOrder == 19 || s->frankNextOrder == 20 || s->frankNextOrder == 21 ||
            s->frankNextOrder == 27 ||
            s->frankNextOrder == 10 || s->frankNextOrder == 11))
            wos_startPair(context, entity, 3, s->frankNextOrder);
        else
            wos_select(context, entity, s->frankEnter ? 12 : 3, 1);
    }
}

static void wmv_update(WarContext* context)
{
    if (wai_drivesTicks(context)) return;   // the AI's tick runs the unit table
    if (context->map != wmv_map)
    {
        wmv_map = context->map;
        wmv_nextTick = context->gameTime;
    }
    s32 guard = 0;
    while (context->gameTime >= wmv_nextTick && guard++ < 8)
    {
        wmv_tick(context);
        wmv_nextTick += WMV_TICK;
    }
    if (guard >= 8) wmv_nextTick = context->gameTime;
}

// The tile a walking unit stands on (wu_getUnitTile and friends).
bool wmv_steppingTile(WarContext* context, WarEntity* entity, vec2* tile)
{
    if (!wu_isUnit(entity)) return false;
    WarUnitComponent* u = we_getUnitComponent(context, entity);
    if (!u || !u->wmvStepping) return false;
    *tile = vec2i(u->wmvTileX, u->wmvTileY);
    return true;
}

// Orders wait for a walking unit's step to end (the original takes them at
// the walk script's checkpoint).
bool wmv_holdsOrders(WarContext* context, WarEntityId id, WarStateType type)
{
    if (type == WAR_STATE_DEATH || type == WAR_STATE_COLLAPSE || type == WAR_STATE_MOVE) return false;
    WarEntity* e = we_findEntity(context, id);
    if (!e || !wu_isUnit(e)) return false;
    WarUnitComponent* u = we_getUnitComponent(context, e);
    return u && u->wmvStepping;
}

// A worker at a mine or a hall steps in (the original's order 12): a move
// that takes one step onto the building.
bool wmv_enterBuilding(WarContext* context, WarEntity* entity, WarEntity* building)
{
    vec2 p = wu_getUnitCenterPosition(context, entity);
    WarStateMove* mv = wst_createMoveState(context, entity, 2, arrayArg(vec2, p, p), false);
    if (!mv) return false;
    mv->frankEnter = building->id;
    if (!wst_pushState(context, entity, (WarStateBase*)mv, WAR_TRANSITION_CAUSE_COMPLETION)) return false;
    return true;
}

// 0x2E554: where a unit comes out of a building: the first free tile on
// rings round it, from the corner nearest (tx, ty), clockwise.
static bool wmv_exitBlocked(WarOriginalRuntime* runtime, s16 x, s16 y)
{
    NOT_USED(runtime);
    const s32 tile = y * WMV_W + x;
    return WMV_STATIC(tile) || wmv_unitAt[tile];
}

bool wmv_findExit(WarContext* context, WarEntity* entity, s32 tx, s32 ty, vec2* result)
{
    WarUnitComponent* unit = we_getUnitComponent(context, entity);
    s32 x = unit->tilex, y = unit->tiley;
    if (!wu_isBuildingUnitType(unit->type)) wmv_unitTile(context, entity, unit, &x, &y);
    s16 ex = (s16)x, ey = (s16)y;
    wmv_buildGrids(context);
    WarOriginalRuntime runtime = { .blocked = wmv_exitBlocked, .error = wmv_terrainError };
    if (wor_findExit(&runtime, &ex, &ey, (u8)unit->sizex, (u8)unit->sizey,
                     (s16)tx, (s16)ty) != WOR_CHANGED) return false;
    *result = vec2i(ex, ey);
    return true;
}

vec2 wmv_exitTile(WarContext* context, WarEntity* building, s32 tx, s32 ty)
{
    vec2 result;
    if (wmv_findExit(context, building, tx, ty, &result)) return result;
    WarUnitComponent* b = we_getUnitComponent(context, building);
    return vec2i(CLAMP(b->tilex - 1, 0, MAP_TILES_WIDTH - 1), CLAMP(b->tiley - 1, 0, MAP_TILES_HEIGHT - 1));
}

// 0x13990: a unit or a building fits at (x, y): its footprint on the map,
// off water, trees, rocks and other units and buildings (a building off
// bridges too).
bool wmv_placeFree(WarContext* context, WarUnitType type, s32 x, s32 y)
{
    const WarUnitData* data = wu_getUnitData(type);
    const s32 w = data ? data->sizex : 1, h = data ? data->sizey : 1;
    if (x < 0 || y < 0 || x + w > MAP_TILES_WIDTH || y + h > MAP_TILES_HEIGHT) return false;
    const bool building = wu_isBuildingUnitType(type);
    const u16 mask = building ? 0x2DC : 0x2CC;   // (0x3CC / 0x3DC without the unit bit)
    for (s32 ty = y; ty < y + h; ty++)
        for (s32 tx = x; tx < x + w; tx++)
            if (wmv_haveTerrain && (wmv_terrain[ty * WMV_W + tx] & mask))
            {
#ifndef FRANK_BOARD
                if (getenv("WAR_MOVEDEBUG")) fprintf(stderr, "  terrain %04x at %d,%d\n", wmv_terrain[ty * WMV_W + tx], tx, ty);
#endif
                return false;
            }
    WarEntityManager* manager = we_getEntityManager(context);
    for (s32 i = 1; i < MAX_ENTITIES_COUNT; i++)
    {
        WarEntity* e = &manager->entities[i];
        if (!e->id || !wu_isUnit(e)) continue;
        WarUnitComponent* u = we_getUnitComponent(context, e);
        if (!u || u->hp <= 0 || wu_isCorpseUnitType(u->type)) continue;
        s32 ux = u->tilex, uy = u->tiley, uw = u->sizex, uh = u->sizey;
        if (!wu_isBuildingUnitType(u->type))
        {
            vec2 t = wu_getUnitCenterTile(context, e);
            ux = (s32)t.x; uy = (s32)t.y; uw = 1; uh = 1;
        }
        if (ux < x + w && x < ux + uw && uy < y + h && y < uy + uh)
        {
#ifndef FRANK_BOARD
            if (getenv("WAR_MOVEDEBUG")) fprintf(stderr, "  under type %d at %d,%d size %d,%d\n", u->type, ux, uy, uw, uh);
#endif
            return false;
        }
    }
    return true;
}

// A tree stands on (x, y): the level's tree, not chopped down.
bool wmv_standingTree(WarContext* context, s32 x, s32 y)
{
    if (!wmv_inside(x, y) || !wmv_haveTerrain) return false;
    NOT_USED(context);
    return wmv_treeUp[y * WMV_W + x] != 0;
}

// war1 took a tree down (war_trees.c)
void wmv_treeDown(s32 x, s32 y)
{
    if (wmv_inside(x, y)) wmv_treeUp[y * WMV_W + x] = 0;
}

// 0x224AC: a walk to a place (no target) ends at the first tile in the way
// on the line there from the unit: from the place back towards the unit
// while the tiles are taken, then a step forward again.
vec2 wmv_backOff(WarContext* context, WarEntity* e, s32 x, s32 y)
{
    WarUnitComponent* u = we_getUnitComponent(context, e);
    s32 ux, uy;
    wmv_unitTile(context, e, u, &ux, &uy);
    WarOriginalLine l;
    wor_lineInit(&l, ux, uy, x, y);
    const s32 gx = x, gy = y;
    s32 dx = 0, dy = 0;
    for (s32 guard = 0; guard < 128; guard++)
    {
        if (x == ux && y == uy) return vec2i(gx, gy);   // (all in the way: the place itself)
        if (!wmv_inside(x, y)) return vec2i(gx, gy);
        const s32 i = y * WMV_W + x;
        const bool taken = WMV_STATIC(i) || wmv_unitAt[i] || wmv_standingTree(context, x, y);
        if (!taken)
        {
            x += dx;
            y += dy;
            break;
        }
        wor_lineStep(&l, &dx, &dy);
        x -= dx;
        y -= dy;
    }
    return vec2i(x, y);
}

// 0x2BCD4: the nearest tree round (x, y), in rings of 3, 5, 7 and 9, each
// from its top left corner clockwise.
bool wmv_treeAround(WarContext* context, s32 x, s32 y, s32* tx, s32* ty)
{
    NOT_USED(context);
    return wmv_haveTerrain && wor_treeAround(wmv_treeUp, x, y, tx, ty);
}

// The tile a walk to a building aims at from where the unit stands.
vec2 wmv_buildingGoal(WarContext* context, WarEntity* e, WarEntity* building)
{
    WarUnitComponent* u = we_getUnitComponent(context, e);
    WarUnitComponent* b = we_getUnitComponent(context, building);
    s32 ux, uy, gx, gy;
    wmv_unitTile(context, e, u, &ux, &uy);
    wmv_goalOnBuilding(b, ux, uy, &gx, &gy);
    return vec2i(gx, gy);
}
