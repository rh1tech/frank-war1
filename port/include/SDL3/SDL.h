/*
 * frank-war1 — the part of SDL3 that war1 uses, on our own platform layer.
 *
 * war1 is written against SDL3. On the board there is no SDL: this header
 * stands in for it, and port/fsdl.c implements it on plat.h (HDMI, USB HID,
 * I2S, FatFs on the board; a real SDL3 window or a headless run on the host).
 *
 * Rendering is software into a 320x200 RGB565 framebuffer. Textures do not
 * own pixels: SDL_UpdateTexture() points a texture at the caller's data,
 * which war1 keeps alive (resource frames, the minimap buffers).
 *
 * Keycodes are USB HID usage codes, the same numbers as SDL scancodes, so
 * the board's HID reports and the host's SDL scancodes map one to one.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: Zlib
 */
#ifndef FRANK_FAKE_SDL_H
#define FRANK_FAKE_SDL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The real SDL3 (host/plat_host.c) lives in the same program on the host:
 * these names must not clash with it. */
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK FSDL_AUDIO_DEVICE_DEFAULT_PLAYBACK
#define SDL_CloseIO FSDL_CloseIO
#define SDL_ConvertEventToRenderCoordinates FSDL_ConvertEventToRenderCoordinates
#define SDL_CreateMutex FSDL_CreateMutex
#define SDL_CreateRenderer FSDL_CreateRenderer
#define SDL_CreateSurfaceFrom FSDL_CreateSurfaceFrom
#define SDL_CreateTexture FSDL_CreateTexture
#define SDL_CreateWindow FSDL_CreateWindow
#define SDL_Delay FSDL_Delay
#define SDL_DestroyAudioStream FSDL_DestroyAudioStream
#define SDL_DestroyMutex FSDL_DestroyMutex
#define SDL_DestroyRenderer FSDL_DestroyRenderer
#define SDL_DestroySurface FSDL_DestroySurface
#define SDL_DestroyTexture FSDL_DestroyTexture
#define SDL_DestroyWindow FSDL_DestroyWindow
#define SDL_GetError FSDL_GetError
#define SDL_GetIOSize FSDL_GetIOSize
#define SDL_GetTicksNS FSDL_GetTicksNS
#define SDL_HideCursor FSDL_HideCursor
#define SDL_IOFromFile FSDL_IOFromFile
#define SDL_Init FSDL_Init
#define SDL_LockMutex FSDL_LockMutex
#define SDL_LogMessage FSDL_LogMessage
#define SDL_OpenAudioDeviceStream FSDL_OpenAudioDeviceStream
#define SDL_PollEvent FSDL_PollEvent
#define SDL_PushEvent FSDL_PushEvent
#define SDL_PutAudioStreamData FSDL_PutAudioStreamData
#define SDL_Quit FSDL_Quit
#define SDL_ReadIO FSDL_ReadIO
#define SDL_RenderClear FSDL_RenderClear
#define SDL_RenderFillRect FSDL_RenderFillRect
#define SDL_RenderLine FSDL_RenderLine
#define SDL_RenderPresent FSDL_RenderPresent
#define SDL_RenderRect FSDL_RenderRect
#define SDL_RenderTexture FSDL_RenderTexture
#define SDL_RenderTextureRotated FSDL_RenderTextureRotated
#define SDL_ResumeAudioStreamDevice FSDL_ResumeAudioStreamDevice
#define SDL_SetLogPriority FSDL_SetLogPriority
#define SDL_SetRenderDrawBlendMode FSDL_SetRenderDrawBlendMode
#define SDL_SetRenderDrawColor FSDL_SetRenderDrawColor
#define SDL_SetRenderLogicalPresentation FSDL_SetRenderLogicalPresentation
#define SDL_SetTextureAlphaMod FSDL_SetTextureAlphaMod
#define SDL_SetTextureBlendMode FSDL_SetTextureBlendMode
#define SDL_SetTextureColorMod FSDL_SetTextureColorMod
#define SDL_SetTextureScaleMode FSDL_SetTextureScaleMode
#define SDL_SetWindowIcon FSDL_SetWindowIcon
#define SDL_SetWindowMouseGrab FSDL_SetWindowMouseGrab
#define SDL_SetWindowSize FSDL_SetWindowSize
#define SDL_SetWindowTitle FSDL_SetWindowTitle
#define SDL_StartTextInput FSDL_StartTextInput
#define SDL_StepUTF8 FSDL_StepUTF8
#define SDL_StopTextInput FSDL_StopTextInput
#define SDL_UnlockMutex FSDL_UnlockMutex
#define SDL_UpdateTexture FSDL_UpdateTexture
#define SDL_WriteIO FSDL_WriteIO
#define SDL_rand FSDL_rand
#define SDL_rand_r FSDL_rand_r
#define SDL_randf FSDL_randf
#define SDL_strlcpy FSDL_strlcpy

#ifdef __cplusplus
extern "C" {
#endif

#define SDLCALL
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int16_t Sint16;
typedef int32_t Sint32;
typedef int64_t Sint64;

typedef uint32_t SDL_InitFlags;
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_VIDEO 0x20u
bool SDL_Init(SDL_InitFlags flags);
void SDL_Quit(void);
const char *SDL_GetError(void);

/* ---- time, random, strings, log ---- */
uint64_t SDL_GetTicksNS(void);
void SDL_Delay(uint32_t ms);
int32_t SDL_rand(int32_t n);
float SDL_randf(void);
int32_t SDL_rand_r(uint64_t *state, int32_t n);
size_t SDL_strlcpy(char *dst, const char *src, size_t maxlen);
#define SDL_strlen strlen
#define SDL_vsnprintf vsnprintf
uint32_t SDL_StepUTF8(const char **pstr, size_t *pslen);

typedef enum {
    SDL_LOG_PRIORITY_INVALID, SDL_LOG_PRIORITY_TRACE, SDL_LOG_PRIORITY_VERBOSE,
    SDL_LOG_PRIORITY_DEBUG, SDL_LOG_PRIORITY_INFO, SDL_LOG_PRIORITY_WARN,
    SDL_LOG_PRIORITY_ERROR, SDL_LOG_PRIORITY_CRITICAL, SDL_LOG_PRIORITY_COUNT
} SDL_LogPriority;
#define SDL_LOG_CATEGORY_APPLICATION 0
void SDL_SetLogPriority(int category, SDL_LogPriority priority);
void SDL_LogMessage(int category, SDL_LogPriority priority, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* ---- mutex ---- */
typedef struct SDL_Mutex SDL_Mutex;
SDL_Mutex *SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_Mutex *m);
void SDL_LockMutex(SDL_Mutex *m);
void SDL_UnlockMutex(SDL_Mutex *m);

/* ---- files ---- */
typedef struct SDL_IOStream SDL_IOStream;
SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode);
int64_t SDL_GetIOSize(SDL_IOStream *s);
size_t SDL_ReadIO(SDL_IOStream *s, void *ptr, size_t size);
size_t SDL_WriteIO(SDL_IOStream *s, const void *ptr, size_t size);
bool SDL_CloseIO(SDL_IOStream *s);

/* ---- window, surface ---- */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Surface SDL_Surface;
typedef uint32_t SDL_PixelFormat;
#define SDL_PIXELFORMAT_RGBA32  1u  /* r,g,b,a bytes; alpha 0 is transparent */
#define SDL_PIXELFORMAT_INDEX8  2u  /* frank: 8-bit, colour 0 transparent, RGB565 LUT */
#define SDL_PIXELFORMAT_FONT2   3u  /* frank: 0 clear, 1 black, 2 white (tinted) */
SDL_Window *SDL_CreateWindow(const char *title, int w, int h, uint64_t flags);
void SDL_DestroyWindow(SDL_Window *w);
bool SDL_SetWindowSize(SDL_Window *w, int width, int height);
bool SDL_SetWindowTitle(SDL_Window *w, const char *title);
bool SDL_SetWindowIcon(SDL_Window *w, SDL_Surface *icon);
bool SDL_SetWindowMouseGrab(SDL_Window *w, bool grabbed);
SDL_Surface *SDL_CreateSurfaceFrom(int w, int h, SDL_PixelFormat f, void *pixels, int pitch);
void SDL_DestroySurface(SDL_Surface *s);
bool SDL_HideCursor(void);
bool SDL_StartTextInput(SDL_Window *w);
bool SDL_StopTextInput(SDL_Window *w);

/* ---- renderer ---- */
typedef struct { float x, y, w, h; } SDL_FRect;
typedef struct { float x, y; } SDL_FPoint;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef enum { SDL_BLENDMODE_NONE = 0, SDL_BLENDMODE_BLEND = 1 } SDL_BlendMode;
typedef enum { SDL_SCALEMODE_NEAREST = 0, SDL_SCALEMODE_LINEAR = 1 } SDL_ScaleMode;
typedef enum { SDL_TEXTUREACCESS_STATIC, SDL_TEXTUREACCESS_STREAMING } SDL_TextureAccess;
typedef enum { SDL_FLIP_NONE = 0, SDL_FLIP_HORIZONTAL = 1, SDL_FLIP_VERTICAL = 2 } SDL_FlipMode;
typedef enum { SDL_LOGICAL_PRESENTATION_DISABLED, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE = 4 } SDL_RendererLogicalPresentation;

SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, const char *name);
void SDL_DestroyRenderer(SDL_Renderer *r);
bool SDL_SetRenderLogicalPresentation(SDL_Renderer *r, int w, int h, SDL_RendererLogicalPresentation mode);
bool SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t red, uint8_t g, uint8_t b, uint8_t a);
bool SDL_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode mode);
bool SDL_RenderClear(SDL_Renderer *r);
bool SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rect);
bool SDL_RenderRect(SDL_Renderer *r, const SDL_FRect *rect);
bool SDL_RenderLine(SDL_Renderer *r, float x1, float y1, float x2, float y2);
bool SDL_RenderPresent(SDL_Renderer *r);

SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, SDL_PixelFormat f, SDL_TextureAccess a, int w, int h);
void SDL_DestroyTexture(SDL_Texture *t);
/* Points the texture at `pixels` (no copy); NULL rect only. */
bool SDL_UpdateTexture(SDL_Texture *t, const void *rect, const void *pixels, int pitch);
bool SDL_SetTextureScaleMode(SDL_Texture *t, SDL_ScaleMode m);
bool SDL_SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode m);
bool SDL_SetTextureAlphaMod(SDL_Texture *t, uint8_t a);
bool SDL_SetTextureColorMod(SDL_Texture *t, uint8_t r, uint8_t g, uint8_t b);
bool SDL_RenderTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src, const SDL_FRect *dst);
bool SDL_RenderTextureRotated(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src, const SDL_FRect *dst,
                              double angle, const SDL_FPoint *center, SDL_FlipMode flip);

/* frank extensions: an 8-bit texture's RGB565 palette (index 0 is
 * transparent), and a texture whose source coordinates are `div` times its
 * pixels (the font atlases: war1 measures glyphs in 8x atlas units). */
void FRANK_SetTextureLut(SDL_Texture *t, const uint16_t *lut);
void FRANK_SetTextureSourceDiv(SDL_Texture *t, int div);
/* Crop: the pixels passed to SDL_UpdateTexture cover only (x, y, w, h) of
 * the texture; everything else is transparent. */
void FRANK_SetTextureCrop(SDL_Texture *t, int x, int y, int w, int h);
uint16_t FRANK_RGB565(uint8_t r, uint8_t g, uint8_t b);

/* ---- events ---- */
typedef uint32_t SDL_Keycode;
typedef uint16_t SDL_Keymod;
#define SDL_KMOD_SHIFT 0x0003u
#define SDL_KMOD_CTRL  0x00C0u
#define SDL_KMOD_ALT   0x0300u

typedef enum {
    SDL_EVENT_QUIT = 0x100,
    SDL_EVENT_WINDOW_HIDDEN = 0x203, SDL_EVENT_WINDOW_MINIMIZED = 0x209,
    SDL_EVENT_WINDOW_FOCUS_GAINED = 0x20E, SDL_EVENT_WINDOW_FOCUS_LOST = 0x20F,
    SDL_EVENT_KEY_DOWN = 0x300, SDL_EVENT_KEY_UP, SDL_EVENT_TEXT_EDITING, SDL_EVENT_TEXT_INPUT,
    SDL_EVENT_MOUSE_MOTION = 0x400, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP,
} SDL_EventType;

#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3

typedef struct { uint32_t type; uint64_t timestamp; SDL_Keycode key; SDL_Keymod mod; bool down; bool repeat; } SDL_KeyboardEvent;
typedef struct { uint32_t type; uint64_t timestamp; char text[8]; } SDL_TextInputEvent;
typedef struct { uint32_t type; uint64_t timestamp; uint32_t state; float x, y, xrel, yrel; } SDL_MouseMotionEvent;
typedef struct { uint32_t type; uint64_t timestamp; uint8_t button; bool down; uint8_t clicks; float x, y; } SDL_MouseButtonEvent;
typedef union SDL_Event {
    uint32_t type;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    uint8_t padding[32];
} SDL_Event;

bool SDL_PollEvent(SDL_Event *e);
bool SDL_PushEvent(SDL_Event *e);
bool SDL_ConvertEventToRenderCoordinates(SDL_Renderer *r, SDL_Event *e);

/* ---- audio ---- */
typedef enum { SDL_AUDIO_U8 = 0x0008, SDL_AUDIO_S16 = 0x8010 } SDL_AudioFormat;
typedef struct { SDL_AudioFormat format; int channels; int freq; } SDL_AudioSpec;
typedef uint32_t SDL_AudioDeviceID;
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0xFFFFFFFFu)
typedef struct SDL_AudioStream SDL_AudioStream;
typedef void (*SDL_AudioStreamCallback)(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);
SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID dev, const SDL_AudioSpec *spec,
                                           SDL_AudioStreamCallback cb, void *userdata);
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *s);
bool SDL_PutAudioStreamData(SDL_AudioStream *s, const void *buf, int len);
void SDL_DestroyAudioStream(SDL_AudioStream *s);

/* ---- keycodes: USB HID usages ---- */
enum {
    SDLK_A = 0x04, SDLK_B, SDLK_C, SDLK_D, SDLK_E, SDLK_F, SDLK_G, SDLK_H, SDLK_I, SDLK_J,
    SDLK_K, SDLK_L, SDLK_M, SDLK_N, SDLK_O, SDLK_P, SDLK_Q, SDLK_R, SDLK_S, SDLK_T,
    SDLK_U, SDLK_V, SDLK_W, SDLK_X, SDLK_Y, SDLK_Z,
    SDLK_1 = 0x1E, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9, SDLK_0,
    SDLK_RETURN = 0x28, SDLK_ESCAPE, SDLK_BACKSPACE, SDLK_TAB, SDLK_SPACE,
    SDLK_MINUS = 0x2D, SDLK_EQUALS, SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET, SDLK_BACKSLASH,
    SDLK_SEMICOLON = 0x33, SDLK_APOSTROPHE, SDLK_GRAVE, SDLK_COMMA, SDLK_PERIOD, SDLK_SLASH,
    SDLK_F1 = 0x3A, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9,
    SDLK_F10, SDLK_F11, SDLK_F12,
    SDLK_INSERT = 0x49, SDLK_HOME, SDLK_PAGEUP, SDLK_DELETE, SDLK_END, SDLK_PAGEDOWN,
    SDLK_RIGHT = 0x4F, SDLK_LEFT, SDLK_DOWN, SDLK_UP,
    SDLK_KP_DIVIDE = 0x54, SDLK_KP_MULTIPLY, SDLK_KP_MINUS, SDLK_KP_PLUS, SDLK_KP_ENTER,
    SDLK_KP_1, SDLK_KP_2, SDLK_KP_3, SDLK_KP_4, SDLK_KP_5, SDLK_KP_6, SDLK_KP_7, SDLK_KP_8,
    SDLK_KP_9, SDLK_KP_0, SDLK_KP_PERIOD,
    SDLK_KP_EQUALS = 0x67,
    SDLK_LCTRL = 0xE0, SDLK_LSHIFT, SDLK_LALT, SDLK_LGUI, SDLK_RCTRL, SDLK_RSHIFT, SDLK_RALT, SDLK_RGUI,
};

#ifdef __cplusplus
}
#endif
#endif
