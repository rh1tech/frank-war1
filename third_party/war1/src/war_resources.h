#pragma once

#include "war_sprites.h"
#include "war_units.h"
#include "war_database.h"

#define WAR_BUILDING_DAMAGE_1_RESOURCE 352
#define WAR_BUILDING_DAMAGE_2_RESOURCE 353
#define WAR_BUILDING_COLLAPSE_RESOURCE 356
#define WAR_EXPLOSION_RESOURCE 354
#define WAR_RAIN_OF_FIRE_EXPLOSION_RESOURCE 351
#define WAR_SPELL_RESOURCE 355
#define WAR_POISON_CLOUD_RESOURCE 350

struct _WarRawResource
{
    bool placeholder;
    bool compressed;
    u32 offset;
    u32 compressedLength;
    u32 length;
    u32 index;
    u8 *data;
};

struct _WarFile
{
    // File Header
    // Version          Archive ID          Number of Entries
    // PreRelease                           EF 01 00 00 (495)   little-endian
    // DOS Retail       18 00 00 00 (24)    47 02 00 00 (583)   little-endian
    // DOS Shareware    19 00 00 00 (25)    47 02 00 00 (583)   little-endian
    // Mac Retail       00 00 00 1A (26)    00 00 02 47 (583)   big-endian
    // Mac Shareware    00 00 00 19 (25)    00 00 02 47 (583)   big-endian
	u32 archiveID;
    u32 numberOfEntries;

    // File Table
    u32 offsets[MAX_RESOURCES_COUNT];

    WarFileType type;
    WarRawResource resources[MAX_RESOURCES_COUNT];
#ifdef WAR_FRANK
    // frank: entries are read from the file when first used
    void* stream;
    u32 fileLength;
#endif
};

struct _WarLevelUnit
{
    u8 x, y;
    WarUnitType type;
    u8 player;
    WarResourceKind resourceKind;
    u16 amount;
};
// WarUnitType and WarResourceKind are int-sized enums; actual layout is 20 bytes, not 12 as spec claims.
static_assert(sizeof(WarLevelUnit) == 20, "WarLevelUnit size mismatch; check enum sizes");

struct _WarLevelConstruct
{
    WarConstructType type;
    u8 x1, y1;
    u8 x2, y2;
    u8 player;
};
// WarConstructType is int-sized; actual layout is 12 bytes, not 8 as spec claims.
static_assert(sizeof(WarLevelConstruct) == 12, "WarLevelConstruct size mismatch; check enum sizes");

struct _WarTilesetTile
{
    struct
    {
        u16 index;
        bool flipX;
        bool flipY;
    } tiles[4];
};

struct _WarCustomMapConfiguration
{
    u32 startEntitiesCount;
    WarLevelUnit* startEntities;
};

#ifdef WAR_FRANK
// frank: the original computer player's parameters, from the level header
// (docs/ai.md "Everything tunable is in the level header")
#define WAR_AI_ROUTE_WORDS_MAX 400
typedef struct _WarLevelAI
{
    u16 flags;                  // 0x8E: AI flags (attacks, objectives)
    u8 alliance[5];             // 0x88: 1 = computer player hostile to player 0
    u16 interval[19];           // 0x36: production interval per building type 0x20..0x32
    u16 workerLimit[5];         // 0x96
    u16 groupSize[5];           // 0xA2: units gathered at a waypoint before moving on
    u16 groupEvery[5];          // 0xAC: groups launched before the size grows
    u16 groupMax[5];            // 0xC0
    u16 exitHeroes;             // 0x92: heroes that must reach the exit (flag 0x04)
    s32 routeWordsCount;        // the route table (after the message records at 0xDC)
    u16 routeWords[WAR_AI_ROUTE_WORDS_MAX];
} WarLevelAI;
#endif

struct _WarResource
{
    WarResourceType type;

    union
    {
        struct
        {
            u8 colors[PALETTE_LENGTH];
        } paletteData;

        struct
        {
            u16 width;
            u16 height;
            u8* pixels;
#ifdef WAR_FRANK
            const u16* lut; // 8-bit pixels, RGB565 palette
#endif
        } imageData;

        struct
        {
            u16 framesCount;
            u8 frameWidth;
            u8 frameHeight;
            WarSpriteFrame* frames;
#ifdef WAR_FRANK
            const u16* lut; // cropped 8-bit frames, RGB565 palette
#endif
        } spriteData;

        struct
        {
            u32 allowId;
            bool allowedHumanUnits;
            bool allowedOrcsUnits;
            bool customMap;
            u8 allowedFeatures[MAX_FEATURES_COUNT];
            u8 allowedUpgrades[MAX_UPGRADES_COUNT][MAX_PLAYERS_COUNT];
            u16 startX;
            u16 startY;
            u16 nextLevelIndex;
            u16 visualIndex;
            u16 passableIndex;
            u16 paletteIndex;
            u16 tilesIndex;
            u16 tilesetIndex;
            u16 tilesetType;
            u32 lumber[MAX_PLAYERS_COUNT];
            u32 gold[MAX_PLAYERS_COUNT];
            WarRace races[MAX_PLAYERS_COUNT];
            char objectives[MAX_OBJECTIVES_LENGTH];

            u32 startEntitiesCount;
            WarLevelUnit* startEntities;

            u32 startRoadsCount;
            WarLevelConstruct* startRoads;

            u32 startWallsCount;
            WarLevelConstruct* startWalls;

            u32 startGoldminesCount;
            WarLevelUnit* startGoldmines;

            u32 startConfigurationsCount;
            WarCustomMapConfiguration startConfigurations[MAX_CUSTOM_MAP_CONFIGURATIONS_COUNT];
#ifdef WAR_FRANK
            WarLevelAI* ai;
#endif
        } levelInfo;

        struct
        {
            u16* data;
        } levelVisual;

        struct
        {
            u16* data;
        } levelPassable;

        struct
        {
            u32 tilesCount;
            u8* data;
#ifdef WAR_FRANK
            const u16* lut;
#endif
        } tilesetData;

        struct
        {
            u16 palette1, palette2;
            u8* data;
        } tilesData;

        struct
        {
            u32 length;
            char* text;
        } textData;

        struct
        {
            u8* data;
            s32 length;
        } audio;

        struct
        {
            u16 hotx;
            u16 hoty;
            u16 width;
            u16 height;
            u8* pixels;
#ifdef WAR_FRANK
            const u16* lut;
#endif
        } cursor;
    };
};

WarResource* wres_getOrCreateResource(WarContext* context, s32 index);
bool wres_tryGetPredefinedCustomMapIndex(StringView name, s32* index);
void wres_loadResource(WarContext* context, DatabaseEntry* entry);

#ifdef WAR_FRANK
bool wres_isLazyResource(s32 index);
void wres_unloadLazyResources(WarContext* context);
#endif
