// frank-war1 — war_sprites.c for the frank build (included by war1.c in its
// place). Sprites made from resources share the resource's 8-bit pixels and
// frames; only sprites war1 builds itself (the minimap, the fog's black
// tile) own RGBA pixels, as upstream.
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib

#include "war_sprites.h"

#include <assert.h>

#include "SDL3/SDL.h"

#include "war_render.h"
#include "war_resources.h"

static SDL_Texture* wspr_newTexture(WarContext* context, u8 format, const u16* lut, u32 width, u32 height)
{
    SDL_Texture* texture = SDL_CreateTexture(context->renderer, format, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (texture && lut)
        FRANK_SetTextureLut(texture, lut);
    return texture;
}

// A single-frame sprite over 8-bit pixels the caller keeps alive.
WarSprite wspr_createIndexedSprite(WarContext* context, u32 width, u32 height, u8 data[], const u16* lut)
{
    WarSprite sprite = (WarSprite){0};
    sprite.frameWidth = width;
    sprite.frameHeight = height;
    sprite.framesCount = 1;
    sprite.format = SDL_PIXELFORMAT_INDEX8;
    sprite.lut = lut;
    sprite.texture = wspr_newTexture(context, SDL_PIXELFORMAT_INDEX8, lut, width, height);

    sprite.frames = (WarSpriteFrame*)wm_alloc(sizeof(WarSpriteFrame));
    sprite.owns = WSPR_OWNS_FRAMES;
    sprite.frames[0] = (WarSpriteFrame){ 0, 0, (u16)width, (u16)height, 0, data };

    if (sprite.texture && data)
        SDL_UpdateTexture(sprite.texture, NULL, data, width);

    return sprite;
}

// The font atlases: 2-bit glyph pixels at 1/div of the coordinates war1 uses.
WarSprite wspr_createFontSprite(WarContext* context, u32 width, u32 height, const u8 data[], s32 div)
{
    WarSprite sprite = (WarSprite){0};
    sprite.frameWidth = width * div;
    sprite.frameHeight = height * div;
    sprite.framesCount = 1;
    sprite.format = SDL_PIXELFORMAT_FONT2;
    sprite.texture = wspr_newTexture(context, SDL_PIXELFORMAT_FONT2, NULL, width, height);
    if (sprite.texture)
    {
        FRANK_SetTextureSourceDiv(sprite.texture, div);
        SDL_UpdateTexture(sprite.texture, NULL, data, width);
    }
    return sprite;
}

WarSprite wspr_createSprite(WarContext *context, u32 width, u32 height, u8 data[])
{
    // RGBA pixels from war1 itself: copied and owned, as upstream.
    WarSprite sprite = (WarSprite){0};
    assert(width <= UINT16_MAX);
    assert(height <= UINT16_MAX);
    sprite.frameWidth = width;
    sprite.frameHeight = height;
    sprite.framesCount = 1;
    sprite.format = SDL_PIXELFORMAT_RGBA32;
    sprite.texture = wspr_newTexture(context, SDL_PIXELFORMAT_RGBA32, NULL, width, height);

    sprite.frames = (WarSpriteFrame*)wm_alloc(sizeof(WarSpriteFrame));
    sprite.owns = WSPR_OWNS_FRAMES | WSPR_OWNS_DATA;
    sprite.frames[0] = (WarSpriteFrame){ 0, 0, (u16)width, (u16)height, 0, (u8*)wm_alloc(width * height * 4) };

    if (data)
        memcpy(sprite.frames[0].data, data, width * height * 4);
    if (sprite.texture)
        SDL_UpdateTexture(sprite.texture, NULL, sprite.frames[0].data, width * 4);

    return sprite;
}

WarSprite wspr_createSpriteFromFrames(WarContext *context, u32 frameWidth, u32 frameHeight, u32 frameCount, WarSpriteFrame frames[])
{
    // RGBA frames from war1 itself (the minimap): copied and owned.
    WarSprite sprite = (WarSprite){0};
    sprite.frameWidth = frameWidth;
    sprite.frameHeight = frameHeight;
    sprite.framesCount = frameCount;
    sprite.format = SDL_PIXELFORMAT_RGBA32;
    sprite.texture = wspr_newTexture(context, SDL_PIXELFORMAT_RGBA32, NULL, frameWidth, frameHeight);

    sprite.frames = (WarSpriteFrame*)wm_alloc(frameCount * sizeof(WarSpriteFrame));
    sprite.owns = WSPR_OWNS_FRAMES | WSPR_OWNS_DATA;
    for (u32 i = 0; i < frameCount; i++)
    {
        sprite.frames[i] = frames[i];
        sprite.frames[i].off = 0;
        sprite.frames[i].data = (u8*)wm_alloc(frameWidth * frameHeight * 4);
        if (frames[i].data)
            memcpy(sprite.frames[i].data, frames[i].data, frameWidth * frameHeight * 4);
    }

    return sprite;
}

WarSprite wspr_createSpriteFromResource(WarContext* context, WarResource* resource, s32 frameIndicesCount, s32 frameIndices[])
{
    assert(resource);

    WarSprite sprite = (WarSprite){0};

    switch(resource->type)
    {
        case WAR_RESOURCE_TYPE_IMAGE:
        {
            sprite = wspr_createIndexedSprite(context, resource->imageData.width, resource->imageData.height,
                                              resource->imageData.pixels, resource->imageData.lut);
            break;
        }

        case WAR_RESOURCE_TYPE_SPRITE:
        {
            sprite.frameWidth = resource->spriteData.frameWidth;
            sprite.frameHeight = resource->spriteData.frameHeight;
            sprite.format = SDL_PIXELFORMAT_INDEX8;
            sprite.lut = resource->spriteData.lut;
            sprite.texture = wspr_newTexture(context, SDL_PIXELFORMAT_INDEX8, sprite.lut, sprite.frameWidth, sprite.frameHeight);

            if (frameIndicesCount > 0)
            {
                sprite.frames = (WarSpriteFrame*)wm_alloc(frameIndicesCount * sizeof(WarSpriteFrame));
                sprite.owns = WSPR_OWNS_FRAMES;
                for (s32 i = 0; i < frameIndicesCount; i++)
                {
                    s32 frameIndex = frameIndices[i];
                    assert(frameIndex >= 0 && frameIndex < (s32)resource->spriteData.framesCount);
                    sprite.frames[i] = resource->spriteData.frames[frameIndex];
                }
                sprite.framesCount = frameIndicesCount;
            }
            else
            {
                sprite.frames = resource->spriteData.frames;
                sprite.framesCount = resource->spriteData.framesCount;
            }
            break;
        }

        case WAR_RESOURCE_TYPE_CURSOR:
        {
            sprite = wspr_createIndexedSprite(context, resource->cursor.width, resource->cursor.height,
                                              resource->cursor.pixels, resource->cursor.lut);
            break;
        }

        default:
        {
            logWarning("Trying to load the resource of type %d as a sprite.", resource->type);
            assert(false);
            break;
        }
    }

    return sprite;
}

WarSprite wspr_createSpriteFromResourceIndex(WarContext* context, WarSpriteResourceRef spriteResourceRef)
{
    WarResource* resource = wres_getOrCreateResource(context, spriteResourceRef.resourceIndex);
    return wspr_createSpriteFromResource(context, resource, spriteResourceRef.frameIndicesCount, spriteResourceRef.frameIndices);
}

void wspr_updateSpriteImage(WarContext *context, WarSprite sprite, u8 data[])
{
    NOT_USED(context);

    if (!sprite.texture)
    {
        logWarning("Trying to update a sprite with no texture");
        return;
    }

    SDL_UpdateTexture(sprite.texture, NULL, data, sprite.frameWidth);

    // a resource sprite's frame: its crop is in the four bytes before it
    if (sprite.format == SDL_PIXELFORMAT_INDEX8 && sprite.framesCount > 0 && sprite.frames != NULL &&
        sprite.frames[0].off == WSPR_CROPPED_FRAME)
    {
        FRANK_SetTextureCrop(sprite.texture, data[-4], data[-3], data[-2], data[-1]);
    }
}

void wspr_renderSubSprite(WarContext *context, WarSprite sprite, rect rs, rect rd, vec2 scale)
{
    if (!sprite.texture)
    {
        logWarning("Trying to render a sprite with no texture");
        return;
    }

    wr_subImage(context, sprite.texture, rs, rd, scale);
}

void wspr_renderSprite(WarContext *context, WarSprite sprite, vec2 pos, vec2 scale)
{
    if (!sprite.texture)
    {
        logWarning("Trying to render a sprite with no texture");
        return;
    }

    vec2 frameSize = vec2i(sprite.frameWidth, sprite.frameHeight);
    rect rs = rectv(VEC2_ZERO, frameSize);
    rect rd = rectv(pos, frameSize);
    wr_subImage(context, sprite.texture, rs, rd, scale);
}

WarSpriteFrame wspr_getSpriteFrame(WarContext* context, WarSprite sprite, s32 frameIndex)
{
    NOT_USED(context);

    assert(sprite.texture);
    assert(frameIndex >= 0 && frameIndex < (s32)sprite.framesCount);
    return sprite.frames[frameIndex];
}

void wspr_freeSprite(WarContext* context, WarSprite sprite)
{
    NOT_USED(context);

    if (!sprite.texture)
    {
        logWarning("Trying to free a sprite with no texture");
        return;
    }

    if (sprite.owns & WSPR_OWNS_DATA)
    {
        for (s32 i = 0; i < sprite.framesCount; i++)
        {
            if (sprite.frames[i].data)
                wm_free(sprite.frames[i].data);
        }
    }

    if (sprite.owns & WSPR_OWNS_FRAMES)
        wm_free(sprite.frames);

    SDL_DestroyTexture(sprite.texture);
}
