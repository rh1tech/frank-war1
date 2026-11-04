// frank-war1 — war1's path searches for the frank build (included by
// war_pathfinder.c in place of its own). Same results as upstream, but:
//  - Dijkstra and A* run on an indexed binary heap over fixed arrays
//    (upstream searched the heap linearly for every neighbour: seconds per
//    flow field on a big map, on the board far worse);
//  - flow fields keep only their directions (4 KB instead of 20 KB) in a
//    small LRU cache instead of one field per tile kept forever;
//  - the empty-tile search is a BFS over a visited bitmap.
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib

#define WPATH_N (MAP_TILES_WIDTH * MAP_TILES_HEIGHT)

// ---- the searches' per-tile state, one 8-byte record (one XIP cache line:
// the arrays live in PSRAM on the board) ----
// gs: bits 0-23 the cost so far (0xFFFFFF: none), bit 24 closed, bits 25-31
// the search it belongs to (a record of an older search reads as fresh, so
// a search starts without clearing 4096 records; they are cleared only
// when the 7-bit stamp wraps).
typedef struct
{
    u32 gs;
    s16 parent;
    s16 pos;       // heap position, -1 = not in heap
} WarPathNode;

#define WPATH_INF 0xFFFFFFu
static WarPathNode wpath_nodes[WPATH_N];
static u32 wpath_stamp;

static inline WarPathNode* wpath_node(s32 n)
{
    WarPathNode* r = &wpath_nodes[n];
    if ((r->gs >> 25) != wpath_stamp)
    {
        r->gs = (wpath_stamp << 25) | WPATH_INF;
        r->parent = -1;
        r->pos = -1;
    }
    return r;
}

static inline bool wpath_fresh(s32 n) { return (wpath_nodes[n].gs >> 25) == wpath_stamp; }
static inline s32 wpath_getG(s32 n) { return wpath_fresh(n) ? (s32)(wpath_nodes[n].gs & WPATH_INF) : (s32)WPATH_INF; }
static inline void wpath_setG(s32 n, s32 g) { WarPathNode* r = wpath_node(n); r->gs = (r->gs & ~WPATH_INF) | ((u32)g & WPATH_INF); }
static inline bool wpath_isClosed(s32 n) { return wpath_fresh(n) && (wpath_nodes[n].gs & (1u << 24)); }
static inline void wpath_setClosed(s32 n) { wpath_node(n)->gs |= 1u << 24; }
static inline s32 wpath_getParent(s32 n) { return wpath_fresh(n) ? wpath_nodes[n].parent : -1; }
static inline void wpath_setParent(s32 n, s32 p) { wpath_node(n)->parent = (s16)p; }

// ---- indexed min-heap: each entry is key << 12 | tile, ordered by the key
// alone (so ties fall exactly as with a separate key array) ----
static u32 wpath_heap[WPATH_N];
static s32 wpath_heapCount;
#define WPATH_HKEY(e) ((e) >> 12)
#define WPATH_HNODE(e) ((s32)((e) & 0xFFFu))

static void wpath_heapSwap(s32 a, s32 b)
{
    u32 ea = wpath_heap[a], eb = wpath_heap[b];
    wpath_heap[a] = eb; wpath_nodes[WPATH_HNODE(eb)].pos = (s16)a;
    wpath_heap[b] = ea; wpath_nodes[WPATH_HNODE(ea)].pos = (s16)b;
}

static void wpath_heapUp(s32 i)
{
    while (i > 0)
    {
        s32 p = (i - 1) / 2;
        if (WPATH_HKEY(wpath_heap[p]) <= WPATH_HKEY(wpath_heap[i]))
            break;
        wpath_heapSwap(i, p);
        i = p;
    }
}

static void wpath_heapDown(s32 i)
{
    for (;;)
    {
        s32 l = 2 * i + 1, r = l + 1, m = i;
        if (l < wpath_heapCount && WPATH_HKEY(wpath_heap[l]) < WPATH_HKEY(wpath_heap[m])) m = l;
        if (r < wpath_heapCount && WPATH_HKEY(wpath_heap[r]) < WPATH_HKEY(wpath_heap[m])) m = r;
        if (m == i)
            break;
        wpath_heapSwap(i, m);
        i = m;
    }
}

static void wpath_heapPushOrDecrease(s32 node, s32 key)
{
    WarPathNode* r = wpath_node(node);
    const u32 e = ((u32)key << 12) | (u32)node;
    if (r->pos < 0)
    {
        wpath_heap[wpath_heapCount] = e;
        r->pos = (s16)wpath_heapCount;
        wpath_heapCount++;
    }
    else
    {
        wpath_heap[r->pos] = e;
    }
    wpath_heapUp(r->pos);
}

static s32 wpath_heapPop(void)
{
    s32 node = WPATH_HNODE(wpath_heap[0]);
    wpath_heapCount--;
    if (wpath_heapCount > 0)
    {
        wpath_heap[0] = wpath_heap[wpath_heapCount];
        wpath_nodes[WPATH_HNODE(wpath_heap[0])].pos = 0;
        wpath_heapDown(0);
    }
    wpath_nodes[node].pos = -1;
    return node;
}

static void wpath_searchReset(void)
{
    if (++wpath_stamp >= 128)
    {
        memset(wpath_nodes, 0, sizeof(wpath_nodes));
        wpath_stamp = 1;
    }
    wpath_heapCount = 0;
}

void wpath_astar(WarPathFinder* finder, s32 startX, s32 startY, s32 endX, s32 endY, WarMapPath* path)
{
    assert(finder);
    assert(inRange(startX, 0, MAP_TILES_WIDTH));
    assert(inRange(startY, 0, MAP_TILES_HEIGHT));
    assert(inRange(endX, 0, MAP_TILES_WIDTH));
    assert(inRange(endY, 0, MAP_TILES_HEIGHT));
    assert(path);

    wpath_searchReset();

    const s32 start = startY * MAP_TILES_WIDTH + startX;
    const s32 end = endY * MAP_TILES_WIDTH + endX;
    s32 closedCount = 0;

    wpath_setG(start, 0);
    wpath_heapPushOrDecrease(start, ABS(startX - endX) + ABS(startY - endY));

    while (wpath_heapCount > 0)
    {
        s32 current = wpath_heapPop();
        wpath_setClosed(current);
        closedCount++;

        if (current == end)
            break;

        s32 cx = current % MAP_TILES_WIDTH, cy = current / MAP_TILES_WIDTH;
        for (s32 d = 0; d < wpath_dirC; d++)
        {
            s32 xx = cx + wpath_dirX[d];
            s32 yy = cy + wpath_dirY[d];
            if (!wpath_isInside(xx, yy) || wpath_isStatic(finder, xx, yy))
                continue;

            s32 n = yy * MAP_TILES_WIDTH + xx;
            if (wpath_isClosed(n))
                continue;

            s32 g = wpath_getG(current) + 1;
            if (g >= wpath_getG(n))
                continue;

            wpath_setG(n, g);
            wpath_setParent(n, current);
            wpath_heapPushOrDecrease(n, g + ABS(xx - endX) + ABS(yy - endY));
        }
    }

    // only process the path if has at least two points
    if (closedCount > 1)
    {
        s32 node = end;

        // unreachable: go to the closest evaluated node instead
        if (!wpath_isClosed(end))
        {
            s32 minDistanceToEnd = INT32_MAX;
            s32 minDistanceFromStart = INT32_MAX;
            for (s32 k = 0; k < WPATH_N; k++)
            {
                if (!wpath_isClosed(k))
                    continue;
                s32 kx = k % MAP_TILES_WIDTH, ky = k / MAP_TILES_WIDTH;
                s32 toEnd = (kx - endX) * (kx - endX) + (ky - endY) * (ky - endY);
                s32 fromStart = (kx - startX) * (kx - startX) + (ky - startY) * (ky - startY);
                if (toEnd < minDistanceToEnd || (toEnd == minDistanceToEnd && fromStart < minDistanceFromStart))
                {
                    node = k;
                    minDistanceToEnd = toEnd;
                    minDistanceFromStart = fromStart;
                }
            }
        }

        path->count = 0;
        while (wpath_getParent(node) >= 0)
        {
            path->nodes[path->count++] = vec2i(node % MAP_TILES_WIDTH, node / MAP_TILES_WIDTH);
            node = wpath_getParent(node);
        }
        path->nodes[path->count++] = vec2i(startX, startY);

        for (s32 i = 0, j = path->count - 1; i < j; i++, j--)
        {
            vec2 temp = path->nodes[i];
            path->nodes[i] = path->nodes[j];
            path->nodes[j] = temp;
        }
    }
}

void wpath_flowField(WarPathFinder* finder, s32 x, s32 y, WarMapFlowField* flowField)
{
    assert(finder);
    assert(inRange(x, 0, MAP_TILES_WIDTH));
    assert(inRange(y, 0, MAP_TILES_HEIGHT));
    assert(flowField);

    wpath_searchReset();

    const s32 start = y * MAP_TILES_WIDTH + x;
    wpath_setG(start, 0);
    wpath_heapPushOrDecrease(start, 0);

    while (wpath_heapCount > 0)
    {
        s32 current = wpath_heapPop();
        wpath_setClosed(current);

        s32 cx = current % MAP_TILES_WIDTH, cy = current / MAP_TILES_WIDTH;
        for (s32 d = 0; d < wpath_dirC; d++)
        {
            s32 xx = cx + wpath_dirX[d];
            s32 yy = cy + wpath_dirY[d];
            if (!wpath_isInside(xx, yy) || wpath_isStatic(finder, xx, yy))
                continue;

            s32 n = yy * MAP_TILES_WIDTH + xx;
            if (wpath_isClosed(n))
                continue;

            // diagonals cost a little more
            s32 g = wpath_getG(current) + ((d % 2 != 0) ? 141 : 100);
            if (g >= wpath_getG(n))
                continue;

            wpath_setG(n, g);
            wpath_setParent(n, current);
            wpath_heapPushOrDecrease(n, g);
        }
    }

    // every reached tile points at its parent, one step towards (x, y)
    for (s32 n = 0; n < WPATH_N; n++)
    {
        s32 p = wpath_getParent(n);
        if (p < 0)
            continue;
        s32 dx = p % MAP_TILES_WIDTH - n % MAP_TILES_WIDTH;
        s32 dy = p / MAP_TILES_WIDTH - n / MAP_TILES_WIDTH;
        for (u8 d = 0; d < wpath_dirC; d++)
        {
            if (dx == wpath_dirX[d] && dy == wpath_dirY[d])
            {
                flowField->dirs[n] = d;
                break;
            }
        }
    }
}

// ---- the flow field cache: one map at a time, so one global pool ----
#define WPATH_FIELDS 32

typedef struct
{
    WarPathFinder* finder;
    s32 tile;      // -1 = free
    u32 lastUsed;
    WarMapFlowField field;
} WarFlowFieldSlot;

static WarFlowFieldSlot* wpath_slots;  // WPATH_FIELDS, allocated on first use
#ifdef WAR_MEMDEBUG
u32 wpath_computes;   // fields computed (host `mem` report)
#endif
static u32 wpath_clock;

static WarFlowFieldSlot* wpath_slotFor(WarPathFinder* finder, s32 tile)
{
    if (!wpath_slots)
    {
        wpath_slots = (WarFlowFieldSlot*)wm_alloc(WPATH_FIELDS * sizeof(WarFlowFieldSlot));
        assert(wpath_slots);
        for (s32 i = 0; i < WPATH_FIELDS; i++)
            wpath_slots[i].tile = -1;
    }

    WarFlowFieldSlot* victim = &wpath_slots[0];
    for (s32 i = 0; i < WPATH_FIELDS; i++)
    {
        WarFlowFieldSlot* slot = &wpath_slots[i];
        if (slot->tile < 0)
        {
            victim = slot;
            break;
        }
        if (slot->lastUsed < victim->lastUsed)
            victim = slot;
    }

    if (victim->tile >= 0 && victim->finder)
        victim->finder->fields[victim->tile] = NULL;

    victim->finder = finder;
    victim->tile = tile;
    return victim;
}

// Drop every cached field of a finder (its map is going away, or its
// static tiles changed).
void wpath_freeFlowFields(WarPathFinder* finder)
{
    if (!wpath_slots)
        return;
    for (s32 i = 0; i < WPATH_FIELDS; i++)
    {
        WarFlowFieldSlot* slot = &wpath_slots[i];
        if (slot->tile >= 0 && slot->finder == finder)
        {
            finder->fields[slot->tile] = NULL;
            slot->tile = -1;
            slot->finder = NULL;
        }
    }
}

static WarMapFlowField* wpath_touch(WarMapFlowField* field)
{
    WarFlowFieldSlot* slot = (WarFlowFieldSlot*)((u8*)field - offsetof(WarFlowFieldSlot, field));
    slot->lastUsed = ++wpath_clock;
    return field;
}

WarMapFlowField* wpath_computeFlowField(WarPathFinder* finder, s32 x, s32 y)
{
    assert(inRange(x, 0, MAP_TILES_WIDTH));
    assert(inRange(y, 0, MAP_TILES_HEIGHT));

    s32 fieldIndex = y * MAP_TILES_WIDTH + x;

    WarMapFlowField* flowField = finder->fields[fieldIndex];
    if (!flowField)
    {
        flowField = &wpath_slotFor(finder, fieldIndex)->field;
        finder->fields[fieldIndex] = flowField;
    }

    memset(flowField->dirs, WAR_DIRECTION_COUNT, sizeof(flowField->dirs));
    wpath_flowField(finder, x, y, flowField);
#ifdef WAR_MEMDEBUG
    wpath_computes++;
#endif

    return wpath_touch(flowField);
}

WarMapFlowField* wpath_getFlowField(WarPathFinder* finder, s32 x, s32 y)
{
    // a goal just off the map (a unit pushed past the edge): the edge tile
    x = CLAMP(x, 0, MAP_TILES_WIDTH - 1);
    y = CLAMP(y, 0, MAP_TILES_HEIGHT - 1);

    WarMapFlowField* field = finder->fields[y * MAP_TILES_WIDTH + x];
    return field ? wpath_touch(field) : NULL;
}

WarMapFlowField* wpath_ensureFlowField(WarPathFinder* finder, s32 x, s32 y)
{
    // a goal just off the map (a unit pushed past the edge): the edge tile
    x = CLAMP(x, 0, MAP_TILES_WIDTH - 1);
    y = CLAMP(y, 0, MAP_TILES_HEIGHT - 1);

    WarMapFlowField* field = finder->fields[y * MAP_TILES_WIDTH + x];
    if (!field)
        return wpath_computeFlowField(finder, x, y);

    return wpath_touch(field);
}

vec2 wpath_findEmptyTile(WarPathFinder* finder, s32 x, s32 y)
{
    if (wpath_isEmpty(finder, x, y))
        return vec2i(x, y);

    // BFS outwards; wpath_heap doubles as the queue, the closed bit as visited
    wpath_searchReset();
    s32 head = 0, tail = 0;
    s32 start = y * MAP_TILES_WIDTH + x;
    wpath_heap[tail++] = (u32)start;
    wpath_setClosed(start);

    while (head < tail)
    {
        s32 current = wpath_heap[head++];
        s32 cx = current % MAP_TILES_WIDTH, cy = current / MAP_TILES_WIDTH;
        if (wpath_isEmpty(finder, cx, cy))
            return vec2i(cx, cy);

        for (s32 d = 0; d < wpath_dirC; d++)
        {
            s32 xx = cx + wpath_dirX[d];
            s32 yy = cy + wpath_dirY[d];
            if (!inRange(xx, 0, MAP_TILES_WIDTH) || !inRange(yy, 0, MAP_TILES_HEIGHT))
                continue;
            s32 n = yy * MAP_TILES_WIDTH + xx;
            if (wpath_isClosed(n))
                continue;
            wpath_setClosed(n);
            wpath_heap[tail++] = (u32)n;
        }
    }

    return vec2i(x, y);
}
