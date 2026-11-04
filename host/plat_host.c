/*
 * frank-war1 — plat.h on the host (macOS/Linux), for developing the port.
 *
 * Headless by default: virtual time advances only while the game sleeps to
 * its frame deadline (1/60 s per frame), so a run is deterministic and as
 * fast as the CPU allows. A script
 * (WAR_SCRIPT=file) drives the mouse and keys and saves frames:
 *
 *   <frame> move X Y            mouse to (X, Y) in 320x200
 *   <frame> down|up L|R X Y     button press or release at (X, Y)
 *   <frame> click L|R X Y       press, and release on the next frame
 *   <frame> key HID down|up     a key by USB HID usage (decimal or 0x..)
 *   <frame> type TEXT           text input (cheats)
 *   <frame> shot FILE.ppm       save the frame
 *   <frame> keym HID MODS down|up  a key with the HID modifier byte (recordings)
 *   <frame> char CODE           one text-input character (recordings)
 *   <frame> hash HEX            the game state must hash to this (replays)
 *   <frame> mem                 print heap use
 *   <frame> quit
 *
 * WAR_RECORD=file opens the window and records what you do in that format,
 * with the game on a fixed 1/60 s step and a state hash every second: the
 * board replays it (tools/replay.sh). WAR_FIXED=1 runs a script on the fixed
 * step too (replaying a recording here checks it is deterministic).
 *
 * WAR_WAV=file.wav records the game's audio (headless: freq/60 samples per
 * frame). WAR_WINDOW=1 opens a real window instead, with live input and audio.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: Zlib
 */
#include <SDL3/SDL.h>

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat.h"

static uint16_t frames[2][PLAT_W * PLAT_H];
static int back;
static uint64_t frame_no;

static bool windowed;
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioStream *astream;
static int audio_freq;
static FILE *wav;
static uint32_t wav_bytes;
static FILE *script;
static char pending[256];
static long pending_frame = -1;
static int release_button;      /* "click": release on the next frame */
static int mx, my;
static uint64_t t0;
static pthread_mutex_t audio_mx = PTHREAD_MUTEX_INITIALIZER;
static FILE *rec;
static int hash_errors, hash_checks;
extern bool war_fixedStep;
extern uint32_t war_stateHash(void *context);
extern void *war_context;

static void wav_header(void) {
    uint32_t rate = (uint32_t)audio_freq;
    uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0};
    memcpy(h + 24, &rate, 4);
    uint32_t br = rate * 2;
    memcpy(h + 28, &br, 4);
    h[32] = 2; h[34] = 16;
    memcpy(h + 36, "data", 4);
    uint32_t riff = 36 + wav_bytes;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 40, &wav_bytes, 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fseek(wav, 0, SEEK_END);
}

static void at_exit(void) {
    if (wav) { wav_header(); fclose(wav); wav = NULL; }
    if (rec) { fprintf(rec, "%llu quit\n", (unsigned long long)frame_no + 1); fclose(rec); rec = NULL; }
    if (hash_checks) printf("host: replay %d/%d state hashes matched\n", hash_checks - hash_errors, hash_checks);
}

bool plat_init(void) {
    windowed = getenv("WAR_WINDOW") && atoi(getenv("WAR_WINDOW"));
    const char *rp = getenv("WAR_RECORD");
    if (rp) {
        if (!(rec = fopen(rp, "w"))) { fprintf(stderr, "host: cannot write %s\n", rp); return false; }
        fprintf(rec, "# frank-war1 recording v1\n");
        /* live play in a window, unless WAR_WINDOW=0 (record a script) */
        windowed = !(getenv("WAR_WINDOW") && !atoi(getenv("WAR_WINDOW")));
        war_fixedStep = true;
    }
    if (getenv("WAR_FIXED") && atoi(getenv("WAR_FIXED"))) war_fixedStep = true;
    const char *sp = getenv("WAR_SCRIPT");
    if (sp && !(script = fopen(sp, "r"))) fprintf(stderr, "host: no script %s\n", sp);
    if (windowed) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return false;
        win = SDL_CreateWindow("frank-war1 (host)", PLAT_W * 3, PLAT_H * 3, 0);
        ren = SDL_CreateRenderer(win, NULL);
        SDL_SetRenderLogicalPresentation(ren, PLAT_W, PLAT_H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, PLAT_W, PLAT_H);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_HideCursor();
    }
    t0 = SDL_GetTicksNS();
    atexit(at_exit);
    return true;
}

uint16_t *plat_backbuffer(void) { return frames[back]; }

static void save_ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "host: cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", PLAT_W, PLAT_H);
    for (int i = 0; i < PLAT_W * PLAT_H; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                          (uint8_t)((c & 31) << 3 | ((c >> 2) & 7))};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("host: frame %llu -> %s\n", (unsigned long long)frame_no, path);
}

extern size_t war_heap_report(char *buf, size_t n);
static void rec_move(int x, int y);
static void rec_button(int b, bool down, int x, int y);
static void rec_key(uint8_t hid, uint8_t mods, bool down);
static void rec_char(char c);
extern size_t war_entity_report(void *context, char *buf, size_t n);
extern size_t wai_report(void *context, char *buf, size_t n);
extern void wai_debug(void *context, const char *cmd);
extern void *war_context;

static void run_script(const uint16_t *shown) {
    if (release_button) {
        rec_button(release_button, false, mx, my);
        release_button = 0;
    }
    while (script) {
        if (pending_frame < 0) {
            if (!fgets(pending, sizeof(pending), script)) { fclose(script); script = NULL; break; }
            char *p = pending;
            while (isspace((unsigned char)*p)) p++;
            if (!*p || *p == '#') continue;
            pending_frame = strtol(p, NULL, 10);
        }
        if ((long)frame_no < pending_frame) break;
        char cmd[16] = "", a[200] = "";
        int x = 0, y = 0;
        sscanf(pending, "%*ld %15s %199[^\n]", cmd, a);
        fflush(stdout);
        if (!strcmp(cmd, "move") && sscanf(a, "%d %d", &x, &y) == 2) {
            mx = x; my = y;
            rec_move(x, y);
        } else if (!strcmp(cmd, "down") || !strcmp(cmd, "up") || !strcmp(cmd, "click")) {
            char b = 'L';
            sscanf(a, " %c %d %d", &b, &x, &y);
            int btn = b == 'R' ? 3 : b == 'M' ? 2 : 1;
            mx = x; my = y;
            rec_move(x, y);
            rec_button(btn, strcmp(cmd, "up") != 0, x, y);
            if (!strcmp(cmd, "click")) release_button = btn;
        } else if (!strcmp(cmd, "key")) {
            char st[8] = "down";
            unsigned code = 0;
            char num[16];
            sscanf(a, "%15s %7s", num, st);
            code = (unsigned)strtoul(num, NULL, 0);
            rec_key((uint8_t)code, 0, !strcmp(st, "down"));
        } else if (!strcmp(cmd, "keym")) {
            unsigned code = 0, mods = 0;
            char st[8] = "down";
            sscanf(a, "%u %u %7s", &code, &mods, st);
            rec_key((uint8_t)code, (uint8_t)mods, !strcmp(st, "down"));
        } else if (!strcmp(cmd, "char")) {
            rec_char((char)atoi(a));
        } else if (!strcmp(cmd, "hash")) {
            const uint32_t want = (uint32_t)strtoul(a, NULL, 16), got = war_stateHash(war_context);
            hash_checks++;
            if (getenv("WAR_HASHPRINT"))
                printf("hashat %llu %08x\n", (unsigned long long)frame_no, got);
            if (got != want) {
                if (!hash_errors) printf("host: frame %llu state hash %08x, recorded %08x: replay diverged\n",
                                         (unsigned long long)frame_no, got, want);
                hash_errors++;
            }
        } else if (!strcmp(cmd, "type")) {
            for (char *c = a; *c; c++) rec_char(*c);
        } else if (!strcmp(cmd, "shot")) {
            save_ppm(a, shown);
        } else if (!strcmp(cmd, "mem")) {
            char buf[256];
            war_heap_report(buf, sizeof(buf));
            printf("host: frame %llu mem %s\n", (unsigned long long)frame_no, buf);
            char e[512];
            war_entity_report(war_context, e, sizeof(e));
            printf("host: frame %llu %s\n", (unsigned long long)frame_no, e);
            wai_report(war_context, e, sizeof(e));
            printf("host: frame %llu %s\n", (unsigned long long)frame_no, e);
        } else if (!strcmp(cmd, "ai")) {
            wai_debug(war_context, a);
        } else if (!strcmp(cmd, "quit")) {
            exit(0);
        }
        pending_frame = -1;
    }
}

/* The game's sound, generated here once per frame like on the board (its
 * callback changes the game: it ends sounds), for a fixed number of
 * samples per frame: freq/60 on average, exactly. */
static uint64_t audio_frames_done;

static void audio_frame(void) {
    if (!audio_freq) return;
    const uint64_t due = (frame_no + 1) * (uint64_t)audio_freq / 60u;
    int16_t buf[2048];
    while (audio_frames_done < due) {
        int n = (int)(due - audio_frames_done);
        if (n > 2048) n = 2048;
        fsdl_audio_render(buf, n);
        audio_frames_done += (uint64_t)n;
        if (astream) SDL_PutAudioStreamData(astream, buf, n * 2);
        if (wav) { fwrite(buf, 2, (size_t)n, wav); wav_bytes += (uint32_t)n * 2; }
    }
}

uint16_t *plat_present(void) {
    const uint16_t *shown = frames[back];
    if (windowed) {
        SDL_UpdateTexture(tex, NULL, shown, PLAT_W * 2);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    audio_frame();
    frame_no++;
    if (rec && frame_no % 60 == 0)
        fprintf(rec, "%llu hash %08x\n", (unsigned long long)frame_no, war_stateHash(war_context));
    run_script(shown);
    back ^= 1;
    return frames[back];
}

/* Headless: time only passes while the game waits for its frame deadline,
 * so every frame is exactly as long as the game asks for. */
static uint64_t virtual_ns;

uint64_t plat_time_ns(void) {
    if (windowed) return SDL_GetTicksNS() - t0;
    return virtual_ns;
}

void plat_sleep_ms(uint32_t ms) {
    if (windowed) SDL_Delay(ms);
    else virtual_ns += ms ? (uint64_t)ms * 1000000ull : 100000ull;
}

static int hid_mods(SDL_Keymod m) {
    int r = 0;
    if (m & SDL_KMOD_LCTRL) r |= 0x01;
    if (m & SDL_KMOD_LSHIFT) r |= 0x02;
    if (m & SDL_KMOD_LALT) r |= 0x04;
    if (m & SDL_KMOD_RCTRL) r |= 0x10;
    if (m & SDL_KMOD_RSHIFT) r |= 0x20;
    if (m & SDL_KMOD_RALT) r |= 0x40;
    return r;
}

/* Live input, recorded when WAR_RECORD is set. */
static int last_mx = -1, last_my = -1;

static void rec_move(int x, int y) {
    fsdl_mouse_move(x, y);
    if (rec && (x != last_mx || y != last_my)) fprintf(rec, "%llu move %d %d\n", (unsigned long long)frame_no, x, y);
    last_mx = x; last_my = y;
}

static void rec_button(int b, bool down, int x, int y) {
    fsdl_mouse_button(b, down, x, y);
    if (rec) fprintf(rec, "%llu %s %c %d %d\n", (unsigned long long)frame_no, down ? "down" : "up",
                     b == 3 ? 'R' : b == 2 ? 'M' : 'L', x, y);
    last_mx = x; last_my = y;
}

static void rec_key(uint8_t hid, uint8_t mods, bool down) {
    fsdl_key(hid, mods, down);
    if (rec) fprintf(rec, "%llu keym %u %u %s\n", (unsigned long long)frame_no, hid, mods, down ? "down" : "up");
}

static void rec_char(char c) {
    fsdl_text(c);
    if (rec) fprintf(rec, "%llu char %d\n", (unsigned long long)frame_no, (unsigned char)c);
}

void plat_poll(void) {
    if (!windowed) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT: exit(0); break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (!e.key.repeat) rec_key((uint8_t)e.key.scancode, (uint8_t)hid_mods(e.key.mod), e.key.down);
            break;
        case SDL_EVENT_TEXT_INPUT:
            for (const char *c = e.text.text; *c; c++) rec_char(*c);
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            SDL_ConvertEventToRenderCoordinates(ren, &e);
            float fx = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
            float fy = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
            int x = (int)fx, y = (int)fy;
            x = x < 0 ? 0 : x >= PLAT_W ? PLAT_W - 1 : x;
            y = y < 0 ? 0 : y >= PLAT_H ? PLAT_H - 1 : y;
            if (e.type == SDL_EVENT_MOUSE_MOTION) rec_move(x, y);
            else rec_button(e.button.button, e.button.down, x, y);
            break;
        }
        }
    }
}

bool plat_audio_open(int freq) {
    audio_freq = freq;
    if (windowed) {
        /* no callback: plat_present() feeds it every frame */
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, freq};
        astream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (astream) SDL_ResumeAudioStreamDevice(astream);
        return astream != NULL;
    }
    const char *wp = getenv("WAR_WAV");
    if (wp && (wav = fopen(wp, "wb"))) wav_header();
    return true;
}

void plat_audio_lock(void) { pthread_mutex_lock(&audio_mx); }
void plat_audio_unlock(void) { pthread_mutex_unlock(&audio_mx); }

void *plat_mutex_create(void) {
    pthread_mutex_t *m = malloc(sizeof(*m));
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    return m;
}
void plat_mutex_lock(void *m) { pthread_mutex_lock(m); }
void plat_mutex_unlock(void *m) { pthread_mutex_unlock(m); }
void plat_mutex_destroy(void *m) { pthread_mutex_destroy(m); free(m); }

void plat_log(int priority, const char *line) {
    static const char *tag[] = {"", "T", "V", "D", "I", "W", "E", "C"};
    fprintf(stderr, "[%s] %s\n", priority >= 0 && priority < 8 ? tag[priority] : "?", line);
}

void plat_quit(void) {}

#include <execinfo.h>
#include <signal.h>
static void on_abort(int sig) {
    void *frames[32];
    int n = backtrace(frames, 32);
    backtrace_symbols_fd(frames, n, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char **argv) {
    signal(SIGABRT, on_abort);
    extern int war_main(int argc, char **argv);
    extern bool wai_autoHuman, wai_autoHumanFree;
    /* test mode: the AI plays the human side too ("free": without paying) */
    const char *auto_human = getenv("WAR_AI_HUMAN");
    wai_autoHuman = auto_human != NULL;
    wai_autoHumanFree = auto_human && !strcmp(auto_human, "free");
    return war_main(argc, argv);
}

/* Music: not on the host yet (the board plays it on OPL2). */
void plat_midi_event(uint8_t status, uint8_t a, uint8_t b) { (void)status; (void)a; (void)b; }
void plat_midi_reset(void) {}
void plat_midi_advance(int samples) { (void)samples; }
