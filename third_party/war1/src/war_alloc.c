#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SHL_MZ_IMPLEMENTATION
#ifdef SHL_MZ_DEBUG
#   define SHL_MZ_AUDIT_IMPLEMENTATION
#   include "shl/memzone_audit.h"
#else
#   include "shl/memzone.h"
#endif

#include "war_alloc.h"
#include "war_log.h"

memzone_t* globalZone = NULL;
memzone_t* frameZone = NULL;
memzone_t* audioZone = NULL;

shl_allocator_t globalAllocator = {0};
shl_allocator_t frameAllocator = {0};
shl_allocator_t audioAllocator = {0};

#ifdef WAR_FRANK
// frank: the zones are the whole budget; track the high-water marks
static size_t peakGlobal, peakFrame, peakAudio;

// frank: long-lived ("global") allocations go to libc's heap, not a memzone.
// The zone's next-fit search left 6 MB free in 650 pieces after loading the
// levels (allocate the worst case, trim, repeat); dlmalloc keeps its free
// space together. Each block carries its size, so the budget (the would-be
// zone size) is still enforced and the use reported as before.
#define WM_HEAP_MAGIC 0x57314850u // "W1HP"
typedef struct { u32 size, magic; } WmHeapHeader;
static size_t heapUsed, heapLimit;

static bool wm__isHeap(void* p)
{
    return p && ((WmHeapHeader*)p - 1)->magic == WM_HEAP_MAGIC;
}

static void* wm__heapAlloc(size_t sz)
{
    if (heapUsed + sz > heapLimit)
        return NULL;
    WmHeapHeader* h = (WmHeapHeader*)calloc(1, sizeof(WmHeapHeader) + sz);
    if (!h)
        return NULL;
    h->size = (u32)sz;
    h->magic = WM_HEAP_MAGIC;
    heapUsed += sz;
    if (heapUsed > peakGlobal) peakGlobal = heapUsed;
    return h + 1;
}

static void wm__heapFree(void* p)
{
    WmHeapHeader* h = (WmHeapHeader*)p - 1;
    heapUsed -= h->size;
    h->magic = 0;
    free(h);
}

static void* wm__heapRealloc(void* p, size_t sz)
{
    WmHeapHeader* h = (WmHeapHeader*)p - 1;
    if (heapUsed - h->size + sz > heapLimit)
        return NULL;
    size_t old = h->size;
    WmHeapHeader* n = (WmHeapHeader*)realloc(h, sizeof(WmHeapHeader) + sz);
    if (!n)
        return NULL;
    if (sz > old)
        memset((u8*)(n + 1) + old, 0, sz - old);
    heapUsed = heapUsed - old + sz;
    if (heapUsed > peakGlobal) peakGlobal = heapUsed;
    n->size = (u32)sz;
    return n + 1;
}

static void* wm__heapAllocFn(void* ctx, size_t sz) { NOT_USED(ctx); return wm__heapAlloc(sz); }
static void* wm__heapReallocFn(void* ctx, void* p, size_t sz)
{
    NOT_USED(ctx);
    if (!p) return wm__heapAlloc(sz);
    return wm__heapRealloc(p, sz);
}
static void wm__heapFreeFn(void* ctx, void* p) { NOT_USED(ctx); if (p) wm__heapFree(p); }
#endif

static void zoneReporter(const memzone_t* zone, mz_report_t report, const void* ptr, const char* message, void* userData)
{
    NOT_USED(userData);

    const char* zoneName = "unknown";
    if (zone == globalZone)
        zoneName = "globalZone";
    else if (zone == frameZone)
        zoneName = "frameZone";
    else if (zone == audioZone)
        zoneName = "audioZone";

    const char* reportName = NULL;
    switch (report)
    {
        case MZ_REPORT_ALLOCATION_FAILURE: reportName = "allocation failure"; break;
        case MZ_REPORT_INVALID_FREE: reportName = "invalid free"; break;
        case MZ_REPORT_VALIDATION_FAILURE: reportName = "validation failure"; break;
        default: reportName = "unknown"; break;
    }

    logError("Memory zone report [%s]: %s, %s, for %p.", zoneName, reportName, message, ptr);
}

static bool initZone(size_t size, const char* logFileName, memzone_t** outZone)
{
#ifdef SHL_MZ_DEBUG
    *outZone = mz_initAudit(size, SHL_MZ_AUDIT_FORMAT_COMPACT, logFileName);
#else
    NOT_USED(logFileName);
    *outZone = mz_init(size);
#endif

    return *outZone;
}

static bool isInZone(memzone_t* zone, void* p)
{
    if (!zone || !p) return false;
    uintptr_t ptr = (uintptr_t)p;
    uintptr_t z = (uintptr_t)zone;
    // maxSize is exactly the size of the whole allocated block
    return ptr >= z && ptr < (z + mz_maxSize(zone));
}

static void* allocInZone(memzone_t* zone, size_t sz, const char* file, int line)
{
#ifdef SHL_MZ_DEBUG
    return mz__audit_alloc(zone, sz, file, line);
#else
    NOT_USED(file);
    NOT_USED(line);
    return mz_alloc(zone, sz);
#endif
}

static void* reallocInZone(memzone_t* zone, void* p, size_t sz, const char* file, int line)
{
#ifdef SHL_MZ_DEBUG
    return mz__audit_realloc(zone, p, sz, file, line);
#else
    NOT_USED(file);
    NOT_USED(line);
    return mz_realloc(zone, p, sz);
#endif
}

static void freeInZone(memzone_t* zone, void* p, const char* file, int line)
{
#ifdef SHL_MZ_DEBUG
    mz__audit_free(zone, p, file, line);
#else
    NOT_USED(file);
    NOT_USED(line);
    mz_free(zone, p);
#endif
}

bool wm_allocInit(size_t globalSize, size_t frameSize, size_t audioSize)
{
#ifdef WAR_FRANK
    heapLimit = globalSize;
    globalSize = 4096; // unused: global allocations live on the heap
#endif
    if (!initZone(globalSize, "global_zone.log", &globalZone))
    {
        logError("Failed to initialize globalZone.");
        return false;
    }

    if (!initZone(frameSize, "frame_zone.log", &frameZone))
    {
        logError("Failed to initialize frameZone.");
        mz_destroy(globalZone);
        globalZone = NULL;
        return false;
    }

    if (!initZone(audioSize, "audio_zone.log", &audioZone))
    {
        logError("Failed to initialize audioZone.");
        mz_destroy(globalZone);
        mz_destroy(frameZone);
        globalZone = NULL;
        frameZone = NULL;
        return false;
    }

#ifdef WAR_FRANK
    globalAllocator = (shl_allocator_t){ NULL, wm__heapAllocFn, wm__heapReallocFn, wm__heapFreeFn };
#else
    globalAllocator = shl_zone_alloc(globalZone);
#endif
    frameAllocator = shl_zone_alloc(frameZone);
    audioAllocator = shl_zone_alloc(audioZone);

    mz_setReporter(globalZone, zoneReporter, NULL);
    mz_setReporter(frameZone, zoneReporter, NULL);
    mz_setReporter(audioZone, zoneReporter, NULL);
    return true;
}

void wm_allocFree(void)
{
    if (globalZone) mz_destroy(globalZone);
    if (frameZone) mz_destroy(frameZone);
    if (audioZone) mz_destroy(audioZone);
    globalZone = NULL;
    frameZone = NULL;
    audioZone = NULL;
    globalAllocator = (shl_allocator_t){0};
    frameAllocator = (shl_allocator_t){0};
    audioAllocator = (shl_allocator_t){0};
}

#ifdef WAR_FRANK

static void* wm__track(memzone_t* zone, size_t* peak, void* p, size_t sz, const char* file, int line)
{
    size_t used = mz_usedSize(zone);
    if (used > *peak) *peak = used;
    if (!p && sz)
    {
        logError("Out of memory: %u bytes at %s:%d (zone %u/%u used)",
                 (unsigned)sz, file, line, (unsigned)used, (unsigned)mz_maxSize(zone));
#ifdef WAR_MEMDEBUG
        static bool dumped;
        if (!dumped)
        {
            dumped = true;
            size_t freeTotal = 0, freeMax = 0, freeN = 0, usedN = 0, usedTotal = 0;
            const memblock_t* b = zone->blockList.next;
            for (; b != &zone->blockList; b = b->next)
            {
                if (b->user == NULL) { freeN++; freeTotal += b->size; if (b->size > freeMax) freeMax = b->size; }
                else { usedN++; usedTotal += b->size; }
            }
            logError("zone dump: %u used blocks %u KB, %u free blocks %u KB, largest free %u, sentinel size %u user %p",
                     (unsigned)usedN, (unsigned)(usedTotal >> 10), (unsigned)freeN, (unsigned)(freeTotal >> 10),
                     (unsigned)freeMax, (unsigned)zone->blockList.size, zone->blockList.user);
        }
#endif
    }
    return p;
}

void* wm__shrink(void* p, size_t sz, const char* file, int line)
{
    if (wm__isHeap(p))
    {
        void* q = wm__heapRealloc(p, sz);
        return q ? q : p;
    }
    if (!p || !isInZone(globalZone, p) || mz_allocationSize(globalZone, p) <= sz + 64)
        return p;
    void* q = wm__alloc(sz, file, line);
    if (!q)
        return p;
    memcpy(q, p, sz);
    wm__free(p, file, line);
    return q;
}

size_t war_heap_report(char* buf, size_t n)
{
    return (size_t)snprintf(buf, n, "global %uK/%uK peak %uK, frame peak %uK, audio %uK peak %uK",
                            (unsigned)(heapUsed >> 10), (unsigned)(heapLimit >> 10),
                            (unsigned)(peakGlobal >> 10), (unsigned)(peakFrame >> 10),
                            (unsigned)(mz_usedSize(audioZone) >> 10), (unsigned)(peakAudio >> 10));
}

void* wm__alloc(size_t sz, const char* file, int line)
{
    void* p = wm__heapAlloc(sz);
#ifdef WAR_MEMDEBUG
    if (sz >= 32768)
        logInfo("bigalloc: %u KB at %s:%d (heap %u KB)", (unsigned)(sz >> 10), file, line, (unsigned)(heapUsed >> 10));
#endif
    if (!p && sz)
        logError("Out of memory: %u bytes at %s:%d (heap %u/%u KB used)",
                 (unsigned)sz, file, line, (unsigned)(heapUsed >> 10), (unsigned)(heapLimit >> 10));
    return p;
}
#else
void* wm__alloc(size_t sz, const char* file, int line)
{
    return allocInZone(globalZone, sz, file, line);
}
#endif

void* wm__allocFrame(size_t sz, const char* file, int line)
{
#ifdef WAR_FRANK
    return wm__track(frameZone, &peakFrame, allocInZone(frameZone, sz, file, line), sz, file, line);
#else
    return allocInZone(frameZone, sz, file, line);
#endif
}

void* wm__allocAudio(size_t sz, const char* file, int line)
{
#ifdef WAR_FRANK
    return wm__track(audioZone, &peakAudio, allocInZone(audioZone, sz, file, line), sz, file, line);
#else
    return allocInZone(audioZone, sz, file, line);
#endif
}

void* wm__realloc(void* p, size_t sz, const char* file, int line)
{
    if (!p) return wm__alloc(sz, file, line);
    if (sz == 0)
    {
        wm__free(p, file, line);
        return NULL;
    }

#ifdef WAR_FRANK
    if (wm__isHeap(p))
    {
        void* q = wm__heapRealloc(p, sz);
        if (!q)
            logError("Out of memory: realloc to %u bytes at %s:%d", (unsigned)sz, file, line);
        return q;
    }
#endif

    if (isInZone(globalZone, p))
    {
        return reallocInZone(globalZone, p, sz, file, line);
    }

    if (isInZone(frameZone, p))
    {
        return reallocInZone(frameZone, p, sz, file, line);
    }

    // fallback, if it's external or stdlib allocated
    NOT_USED(file);
    NOT_USED(line);
    return realloc(p, sz);
}

void* wm__reallocAudio(void* p, size_t sz, const char* file, int line)
{
    if (!p) return wm__allocAudio(sz, file, line);
    if (sz == 0)
    {
        wm__free(p, file, line);
        return NULL;
    }

    return reallocInZone(audioZone, p, sz, file, line);
}

void wm__free(void* p, const char* file, int line)
{
    if (!p) return;

#ifdef WAR_FRANK
    if (!isInZone(frameZone, p) && !isInZone(audioZone, p) && wm__isHeap(p))
    {
        wm__heapFree(p);
        return;
    }
#endif

    if (isInZone(globalZone, p))
    {
        freeInZone(globalZone, p, file, line);
    }
    else if (isInZone(frameZone, p))
    {
        freeInZone(frameZone, p, file, line);
    }
    else if (isInZone(audioZone, p))
    {
        freeInZone(audioZone, p, file, line);
    }
    else
    {
        NOT_USED(file);
        NOT_USED(line);
        free(p); // fallback
    }
}

shl_allocator_t* wm_globalAllocator(void)
{
    return &globalAllocator;
}

shl_allocator_t* wm_frameAllocator(void)
{
    return &frameAllocator;
}

shl_allocator_t* wm_audioAllocator(void)
{
    return &audioAllocator;
}
