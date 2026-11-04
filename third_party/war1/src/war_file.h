#pragma once

#include "shl/wstr.h"

#include "war.h"
#include "war_resources.h"
#include "war_fwd.h"

struct _WarMapFileHeader
{
    u32 magic;
    u16 version;
    u16 flags;
    u32 reserved[2];
};

WarFile* wfile_loadWarFile(WarContext* context, StringView filePath, const bool skipDecompress[]);
void wfile_freeWarFile(WarFile* warFile);
#ifdef WAR_FRANK
// Read and decompress entry `index` into warFile->resources[index].data;
// wfile_releaseResource() frees it again.
bool wfile_readResource(WarFile* warFile, s32 index);
void wfile_releaseResource(WarFile* warFile, s32 index);
#endif
bool wfile_loadWarMapFile(StringView filePath, WarResource* levelInfoRes, WarResource* visualInfoRes, WarResource* passableInfoRes);
