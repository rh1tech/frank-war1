/*
 * frank-war1 — Warcraft: Orcs & Humans on one RP2350B: boot, the welcome
 * and error screens, the SD card, the console, then the game.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Core 0 runs the game (war1, drawing RGB565 through port/fsdl.c), core 1
 * the HDMI scanout and the audio (plat_frank.c: the game's sound upsampled,
 * OPL2 music added, out to the I2S DAC).
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "hardware/structs/systick.h"
#include "pico/bootrom.h"

#include "clocks.h"
#include "console.h"
#include "display.h"
#include "shell.h"
#include "settings.h"
#include "ff.h"
#include "frank_game.h"
#include "psram_init.h"
#include "usb/usbhid.h"
#include "audio/audio.h"
#include "video/hstx_video.h"
#include "opl_war.h"

#define DATA_DIR "/war1"

/* PSRAM chip select: GPIO 47 on an RP2350B (Core 2 master, M2 with a B),
 * GPIO 8 on an RP2350A (M2 boards with the A package). */
static uint psram_cs_pin(void) {
    const uint32_t package_sel = *(io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET);
    return (package_sel & 1) ? 8u : 47u;
}

/* Clocks and PSRAM come up before main(): the game's zero-initialised data
 * lives in PSRAM (memmap_war1.ld). The entry runs on both cores (SDK 2.2);
 * core 1 must not touch PSRAM again. */
extern char __psram_bss_start[], __psram_bss_end[];
extern char __psram_data_start[], __psram_data_end[], __psram_data_load[];
extern char __sram_hot_start[], __sram_hot_end[];
static bool clocks_ok;

#ifdef FRANK_SWD_STAGE
/* SWD bisect build: stop after init stage N and idle, saying so on the UART
 * and taking `bootsel`, so the debug port can be checked at each stage. */
static void swd_park(int stage) {
    if (stage != FRANK_SWD_STAGE) return;
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    const char *want = "bootsel";
    int got = 0;
    for (uint32_t spin = 0;; spin++) {
        if (spin % 20000000u == 0) {
            const char *t = "swdtest stage ";
            while (*t) uart_putc_raw(uart0, *t++);
            uart_putc_raw(uart0, (char)('0' + stage));
            uart_putc_raw(uart0, '\r');
            uart_putc_raw(uart0, '\n');
        }
        if (uart_is_readable(uart0)) {
            const char c = (char)uart_getc(uart0);
            got = c == want[got] ? got + 1 : (c == want[0] ? 1 : 0);
            if (!want[got]) reset_usb_boot(0, 0);
        }
    }
}
#else
#define swd_park(stage) ((void)0)
#endif

static void frank_preinit(void) {
    if (get_core_num() != 0) return;
    swd_park(0);
    clocks_ok = frank_clocks_init();
    swd_park(1);
    psram_init_with_freq(psram_cs_pin(), FRANK_PSRAM_MAX_MHZ);
    swd_park(2);
    memcpy(__psram_data_start, __psram_data_load, (size_t)(__psram_data_end - __psram_data_start));
    memset(__psram_bss_start, 0, (size_t)(__psram_bss_end - __psram_bss_start));
    memset(__sram_hot_start, 0, (size_t)(__sram_hot_end - __sram_hot_start));
}
__attribute__((section(".preinit_array"), used))
static void (*const frank_preinit_entry)(void) = frank_preinit;

static FATFS fs;
static bool sd_mounted;

static bool sd_mount(void) {
    f_unmount("0:");
    /* A card caught mid-transfer by a reset can miss the first
     * initialisation: try a few times. */
    FRESULT r = FR_NOT_READY;
    for (int i = 0; i < 5 && r != FR_OK; i++) {
        if (i) sleep_ms(100);
        r = f_mount(&fs, "0:", 1);
    }
    sd_mounted = r == FR_OK;
    if (!sd_mounted) printf("sd: no microSD (%d)\n", r);
    return sd_mounted;
}

/* ---- Frames ---- */
static volatile uint32_t frames_shown;
/* Core 0 is running a console command (rxfile can take minutes): no frames
 * then is not a hang. */
static volatile bool console_busy;

void frank_frame_shown(void) {
    frames_shown++;
}

/* ---- /war1/launch.cfg: bench-only `key=value` lines ----
 * (the player's settings are /war1/frank.cfg, settings.c; without this file
 * the game starts from the beginning, the Blizzard logo)
 *   race=human|orc       with mission=: start that campaign mission directly
 *   mission=N|custom     (custom: map=forest_1 ... dungeon_7[_big_enemy])
 *   map=NAME  seed=N     custom game map and random seed
 *   memlog=1             heap use on the console every 10 s */
static char cfg_race[16], cfg_mission[16], cfg_map[40], cfg_seed[16];
static bool memlog_on;
extern volatile uint32_t frank_mix_mask;

static void load_launch_cfg(void) {
    FIL f;
    char line[80];
    if (f_open(&f, DATA_DIR "/launch.cfg", FA_READ) != FR_OK) return;
    while (f_gets(line, sizeof(line), &f)) {
        int v;
        sscanf(line, "race=%15s", cfg_race);
        sscanf(line, "mission=%15s", cfg_mission);
        sscanf(line, "map=%39s", cfg_map);
        sscanf(line, "seed=%15s", cfg_seed);
        if (sscanf(line, "memlog=%d", &v) == 1) memlog_on = v != 0;
        if (sscanf(line, "fogfix=%d", &v) == 1) { extern bool war_fogEdgeFix; war_fogEdgeFix = v != 0; }
    }
    f_close(&f);
    printf("launch.cfg: race=%s mission=%s map=%s seed=%s\n", cfg_race, cfg_mission, cfg_map, cfg_seed);
}

/* ---- Console ---- */

extern size_t war_heap_report(char *buf, size_t n);

static void memlog_report(void) {
    struct mallinfo mi = mallinfo();
    char zones[160];
    war_heap_report(zones, sizeof(zones));
    printf("memlog t=%lu s: libc heap used %lu KB free %lu KB (+%lu KB unclaimed) | %s\n",
           (unsigned long)(time_us_64() / 1000000u), (unsigned long)(mi.uordblks >> 10),
           (unsigned long)(mi.fordblks >> 10), (unsigned long)(frank_heap_unclaimed() >> 10), zones);
}

static void cmd_memlog(int argc, char **argv) {
    memlog_on = !(argc > 1 && !strcmp(argv[1], "off"));
    memlog_report();
}

static uint32_t game_fps_x10, hdmi_fps;
static bool stat_on;

static void cmd_fps(int argc, char **argv) {
    printf("fps: game %lu.%lu, hdmi %lu\n", (unsigned long)(game_fps_x10 / 10),
           (unsigned long)(game_fps_x10 % 10), (unsigned long)hdmi_fps);
}

static void cmd_cam(int argc, char **argv) {
    extern size_t wai_camera_report(void *context, char *buf, size_t n);
    extern void *war_context;
    char buf[160];
    wai_camera_report(war_context, buf, sizeof(buf));
    printf("%s\n", buf);
}

static void cmd_stat(int argc, char **argv) {
    stat_on = !(argc > 1 && !strcmp(argv[1], "off"));
}

static void cmd_mem(int argc, char **argv) {
    extern char __bss_end__[], __StackLimit[];
    printf("psram: bss %lu KB, heap %lu KB; sram free %lu KB\n",
           (unsigned long)((__psram_bss_end - __psram_bss_start) >> 10),
           (unsigned long)(frank_heap_used() >> 10),
           (unsigned long)((__StackLimit - __bss_end__) >> 10));
    memlog_report();
}

static void cmd_ls(int argc, char **argv) {
    if (!sd_mounted && !sd_mount()) return;
    DIR dir;
    FILINFO fi;
    const char *path = argc > 1 ? argv[1] : ".";
    if (f_opendir(&dir, path) != FR_OK) { printf("ls: cannot open %s\n", path); return; }
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0])
        printf("%10lu %s%s\n", (unsigned long)fi.fsize, fi.fname, (fi.fattrib & AM_DIR) ? "/" : "");
    f_closedir(&dir);
}

static void cmd_usb(int argc, char **argv) {
    printf("usb: keyboard %s, mouse %s\n", usbhid_keyboard_connected() ? "yes" : "no",
           usbhid_mouse_connected() ? "yes" : "no");
}

static void cmd_snd(int argc, char **argv) {
    extern volatile uint32_t opl_war_writes, opl_war_events;
    printf("audio: rate %lu, starved %lu, queued %lu; music %lu events, %lu OPL writes; mix %lu\n",
           (unsigned long)audio_get_rate(), (unsigned long)audio_starved, (unsigned long)audio_queued(),
           (unsigned long)opl_war_events, (unsigned long)opl_war_writes, (unsigned long)frank_mix_mask);
    printf("hdmi audio: %s, ring space %lu, underruns %lu, drops %lu\n", video_audio_enabled() ? "on" : "off",
           (unsigned long)video_audio_space(), (unsigned long)video_audio_underruns,
           (unsigned long)video_audio_drops);
    extern volatile uint32_t frank_peak_sound, frank_peak_mix, frank_hdmi_clipped, opl_war_peak;
    printf("peaks since last: sound %lu, music %lu, mix %lu (of 32767); hdmi clipped %lu\n",
           (unsigned long)frank_peak_sound, (unsigned long)opl_war_peak, (unsigned long)frank_peak_mix,
           (unsigned long)frank_hdmi_clipped);
    frank_peak_sound = opl_war_peak = frank_peak_mix = 0;
}

static void cmd_mix(int argc, char **argv) {
    if (argc > 1) frank_mix_mask = (uint32_t)atoi(argv[1]);
    cmd_snd(0, NULL);
}

/* Every software restart goes through here: the PSRAM back to SPI mode
 * first (left in QPI it hangs the boot ROM), core 1 stopped. */
static void __attribute__((noreturn)) frank_reboot(void) {
    printf("rebooting\n");
    sleep_ms(50);
    multicore_reset_core1();
    save_and_disable_interrupts();
    psram_reset_to_spi();
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}

static void cmd_reboot(int argc, char **argv) {
    frank_reboot();
}

/* bootsel: restart into the USB bootloader, for picotool (tools/flash.sh
 * --uart: no button, no SWD). */
static void cmd_bootsel(int argc, char **argv) {
    printf("bootsel\n");
    sleep_ms(50);
    multicore_reset_core1();
    save_and_disable_interrupts();
    psram_reset_to_spi();
    reset_usb_boot(0, 0);
}

static void cmd_errtest(int argc, char **argv);

/* rxfile <bytes> <path>: receive a file over this console (tools/push.py):
 * 512-byte blocks, each acknowledged with '.', then the CRC-32. The game
 * stops while it runs. */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
            table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static void cmd_rxfile(int argc, char **argv) {
    if (argc < 3) { printf("rxfile <bytes> <path>\n"); return; }
    uint32_t left = (uint32_t)strtoul(argv[1], NULL, 0);
    FIL f;
    if (f_open(&f, argv[2], FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) { printf("ERR rxfile: cannot create %s\n", argv[2]); return; }
    static uint8_t block[512];
    uint32_t crc = 0, total = 0;
    bool ok = true;
    printf("rxfile: ready\n");
    while (left && ok) {
        const uint32_t n = left > sizeof(block) ? sizeof(block) : left;
        for (uint32_t i = 0; i < n; i++) {
            const int c = console_getc(2000000);
            if (c == PICO_ERROR_TIMEOUT) { ok = false; break; }
            block[i] = (uint8_t)c;
        }
        if (!ok) break;
        UINT put;
        if (f_write(&f, block, n, &put) != FR_OK || put != n) { ok = false; break; }
        crc = crc32_update(crc, block, n);
        total += n;
        left -= n;
        putchar('.');
        stdio_flush();
    }
    f_close(&f);
    printf("\nrxfile: %s %lu bytes crc %08lx%s\n", argv[2], (unsigned long)total, (unsigned long)crc,
           ok ? "" : " FAILED");
}

/* replay [path]: restart and replay a recording made on the host. */
#define REPLAY_MAGIC 0x52504c59u   /* "RPLY" in watchdog scratch 0 */
static char replay_path[64] = DATA_DIR "/replay.txt";

static void cmd_replay(int argc, char **argv) {
    watchdog_hw->scratch[0] = REPLAY_MAGIC;
    /* the path survives the restart in scratch 1 as an index, the speed in 2 */
    watchdog_hw->scratch[1] = argc > 1 ? (uint32_t)atoi(argv[1]) : 0;
    watchdog_hw->scratch[2] = argc > 2 ? (uint32_t)atoi(argv[2]) : 1;
    frank_reboot();
}

/* A replay's first line "#! <game arguments>" (an agent game: "#! --race
 * human --mission 3") starts it as the recording did; else at the logo. */
static char replay_args[96];

static int replay_launch_args(const char *path, char **argv, int max) {
    FIL f;
    int n = 0;
    if (f_open(&f, path, FA_READ) != FR_OK) return 0;
    if (f_gets(replay_args, sizeof(replay_args), &f) && !strncmp(replay_args, "#!", 2)) {
        for (char *p = strtok(replay_args + 2, " \t\r\n"); p && n < max; p = strtok(NULL, " \t\r\n"))
            argv[n++] = p;
    }
    f_close(&f);
    return n;
}

/* prof [ms]: a sampling profiler on core 0. SysTick interrupts it 4000
 * times a second and records the interrupted PC; the hottest addresses are
 * printed for tools/prof.sh (addr2line on the ELF). */
#define PROF_SLOTS 512
/* bench only: in PSRAM (the SRAM is the frames' and HDMI audio's) */
static volatile uint32_t prof_pc[PROF_SLOTS] __attribute__((section(".psram_bss")));
static volatile uint32_t prof_hits[PROF_SLOTS] __attribute__((section(".psram_bss")));
static volatile uint32_t prof_total, prof_lost;
static volatile bool prof_on;

/* Between profiles SysTick keeps core 0's last PCs (and LRs) at 1 kHz, for
 * the hang report core 1 prints when the game stops (frank_hang_check). */
#define HANG_RING 64
static volatile uint32_t hang_pc[HANG_RING], hang_lr[HANG_RING], hang_idx;

void __attribute__((used)) frank_prof_sample(uint32_t pc, uint32_t lr) {
    if (!prof_on) {
        const uint32_t i = hang_idx++ % HANG_RING;
        hang_pc[i] = pc;
        hang_lr[i] = lr;
        return;
    }
    prof_total++;
    pc &= ~0xFu;   /* 16-byte buckets */
    uint32_t h = (pc >> 4) * 2654435761u >> 22;
    for (int i = 0; i < 8; i++, h = (h + 1) & (PROF_SLOTS - 1)) {
        if (prof_pc[h] == pc) { prof_hits[h]++; return; }
        if (!prof_pc[h]) { prof_pc[h] = pc; prof_hits[h] = 1; return; }
    }
    prof_lost++;
}

void __attribute__((naked)) isr_systick(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "ldr r1, [r0, #20]\n"
        "ldr r0, [r0, #24]\n"
        "b frank_prof_sample\n");
}

static void cmd_prof(int argc, char **argv) {
    const uint32_t ms = argc > 1 ? (uint32_t)atoi(argv[1]) : 3000u;
    for (int i = 0; i < PROF_SLOTS; i++) prof_pc[i] = prof_hits[i] = 0;
    prof_total = prof_lost = 0;
    systick_hw->rvr = (uint32_t)(clock_get_hz(clk_sys) / 4000u) - 1u;
    systick_hw->cvr = 0;
    systick_hw->csr = 0x7;   /* processor clock, interrupt, enable */
    prof_on = true;
    /* the game keeps running: sample while it renders (this returns at
     * once; `prof` again prints) */
    (void)ms;
}

static void hang_sampling_start(void) {
    systick_hw->rvr = (uint32_t)(clock_get_hz(clk_sys) / 1000u) - 1u;
    systick_hw->cvr = 0;
    systick_hw->csr = 0x7;
}

/* Core 1: the game (core 0) has shown no frame for 3 s. Print where core 0
 * is, by raw UART (core 0 may hold the stdio lock), and take `bootsel`. */
static void hang_puts(const char *t) { while (*t) uart_putc_raw(uart0, *t++); }
static void hang_hex(uint32_t v) {
    char b[10];
    for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[8] = ' ';
    b[9] = 0;
    hang_puts(b);
}

void frank_hang_check(void) {
    static uint32_t seen_frames;
    static uint64_t seen_at;
    static int got;
    static bool reported;
    const uint64_t now = time_us_64();
    if (frames_shown != seen_frames || !frames_shown || console_busy) {
        seen_frames = frames_shown;
        seen_at = now;
        reported = false;
        return;
    }
    if (now - seen_at < 3000000) return;
    if (!reported || now - seen_at > 10000000) {
        reported = true;
        seen_at = now - 3000000;
        hang_puts("\r\nHANG core 0, frame ");
        hang_hex(frames_shown);
        hang_puts("\r\nhang pc/lr: ");
        const uint32_t end = hang_idx;
        for (uint32_t k = 0; k < HANG_RING; k++) {
            const uint32_t i = (end + k) % HANG_RING;
            hang_hex(hang_pc[i]);
            hang_hex(hang_lr[i]);
            if (k % 4 == 3) hang_puts("\r\n  ");
        }
        hang_puts("\r\nhang end\r\n");
    }
    const char *want = "bootsel";
    while (uart_is_readable(uart0)) {
        const char c = (char)uart_getc(uart0);
        got = c == want[got] ? got + 1 : (c == want[0] ? 1 : 0);
        if (!want[got]) reset_usb_boot(0, 0);
    }
}

static void cmd_profdump(int argc, char **argv) {
    prof_on = false;
    hang_sampling_start();
    printf("prof: %lu samples, %lu lost\n", (unsigned long)prof_total, (unsigned long)prof_lost);
    for (int i = 0; i < PROF_SLOTS; i++)
        if (prof_pc[i] && prof_hits[i] * 1000u >= prof_total * 2u)   /* >= 0.2 % */
            printf("prof %08lx %lu\n", (unsigned long)prof_pc[i], (unsigned long)prof_hits[i]);
    printf("prof end\n");
}

/* Scripted input from the console (bench without a USB mouse or keyboard):
 * the same events plat_frank.c makes from USB. Runs on core 0. */
#include "plat.h"
static int click_button;      /* released on the next service */
static int mouse_x, mouse_y;

static void cmd_mouse(int argc, char **argv) {
    if (argc < 3) { printf("mouse <x> <y> [L|R|M] [click|down|up]\n"); return; }
    mouse_x = atoi(argv[1]);
    mouse_y = atoi(argv[2]);
    plat_set_cursor(mouse_x, mouse_y);
    fsdl_mouse_move(mouse_x, mouse_y);
    if (argc < 4) return;
    const int b = argv[3][0] == 'R' ? 3 : argv[3][0] == 'M' ? 2 : 1;
    const char *how = argc > 4 ? argv[4] : "click";
    if (!strcmp(how, "up")) fsdl_mouse_button(b, false, mouse_x, mouse_y);
    else {
        fsdl_mouse_button(b, true, mouse_x, mouse_y);
        if (!strcmp(how, "click")) click_button = b;
    }
}

static struct { int code; uint64_t up_at; } tapped[8];

static void cmd_key(int argc, char **argv) {
    if (argc < 2) { printf("key <HID usage hex> [down|up|tap]\n"); return; }
    const int code = (int)strtol(argv[1], NULL, 16);
    const char *how = argc > 2 ? argv[2] : "tap";
    /* the settings window's keys, as from the USB keyboard */
    if (settings_is_open()) {
        if (strcmp(how, "up")) settings_key((uint8_t)code);
        return;
    }
    if (code == 0x45) {
        if (strcmp(how, "up")) settings_request();
        return;
    }
    if (!strcmp(how, "up")) { fsdl_key((uint8_t)code, 0, false); return; }
    fsdl_key((uint8_t)code, 0, true);
    if (!strcmp(how, "tap"))
        for (int i = 0; i < 8; i++)
            if (!tapped[i].code) { tapped[i].code = code; tapped[i].up_at = time_us_64() + 100000; break; }
}

static void cmd_tone(int argc, char **argv) {
    extern volatile uint32_t frank_tone_hz;
    frank_tone_hz = argc > 1 ? (uint32_t)atoi(argv[1]) : 0;
}

static void cmd_campaign(int argc, char **argv) {
    extern void plat_campaign(bool on);
    plat_campaign(argc < 2 || strcmp(argv[1], "off"));
}

static void cmd_speed(int argc, char **argv) {
    extern void plat_set_speed(uint32_t speed);
    plat_set_speed(argc > 1 ? (uint32_t)atoi(argv[1]) : 1);
}

static void cmd_menu(int argc, char **argv) {
    settings_request();
}

static void cmd_type(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) fsdl_text(' ');
        for (const char *c = argv[i]; *c; c++) fsdl_text(*c);
    }
}

static const console_cmd_t cmds[] = {
    { "fps",    cmd_fps,    "game and HDMI frames per second (last 2 s)" },
    { "cam",    cmd_cam,    "the game's camera and mouse state" },
    { "rxfile", cmd_rxfile, "rxfile <bytes> <path>: receive a file (tools/push.py)" },
    { "replay", cmd_replay, "replay [n] [speed]: restart and replay " DATA_DIR "/replay.txt (or replay<n>.txt), speed x faster" },
    { "prof",   cmd_prof,   "prof: start sampling core 0 (profdump prints)" },
    { "profdump", cmd_profdump, "profdump: stop sampling, print the hottest addresses" },
    { "mouse",  cmd_mouse,  "mouse <x> <y> [L|R|M] [click|down|up]: move the cursor, press" },
    { "key",    cmd_key,    "key <HID hex> [down|up|tap]: a key (28 enter, 29 esc, 2c space)" },
    { "type",   cmd_type,   "type <text>: text input (cheats)" },
    { "tone",   cmd_tone,   "tone <Hz>: a sine instead of the sound (0: off)" },
    { "campaign", cmd_campaign, "campaign [off]: play each level entered by /war1/L<level>.txt" },
    { "speed",  cmd_speed,  "speed <n>: play n times faster (draws one frame in n, silent)" },
    { "menu",   cmd_menu,   "open the settings window (F12); key <hex> drives it" },
    { "stat",   cmd_stat,   "stat [off]: print fps every 2 s" },
    { "mem",    cmd_mem,    "memory use" },
    { "memlog", cmd_memlog, "memlog [off]: heap use every 10 s" },
    { "ls",     cmd_ls,     "ls [path]: list a directory on the microSD" },
    { "usb",    cmd_usb,    "what the USB host sees" },
    { "snd",    cmd_snd,    "audio and music counters" },
    { "mix",    cmd_mix,    "mix <0-3>: bit 0 sound, bit 1 music" },
    { "errtest", cmd_errtest, "errtest sd|data|game: show an error screen, restart after 8 s" },
    { "reboot", cmd_reboot, "restart" },
    { "bootsel", cmd_bootsel, "restart into the USB bootloader (picotool)" },
    { NULL, NULL, NULL },
};

void frank_service(void) {
    if (click_button) {
        fsdl_mouse_button(click_button, false, mouse_x, mouse_y);
        click_button = 0;
    }
    console_set_commands(cmds);
    console_busy = true;
    console_service();
    console_busy = false;
    const uint64_t now = time_us_64();
    for (int i = 0; i < 8; i++)
        if (tapped[i].code && now >= tapped[i].up_at) {
            fsdl_key((uint8_t)tapped[i].code, 0, false);
            tapped[i].code = 0;
        }
    static uint64_t last, memlog_last;
    static uint32_t last_frames, last_hdmi;
    if (now - last >= 2000000) {
        const uint32_t f = frames_shown, h = display_frames();
        game_fps_x10 = (uint32_t)((uint64_t)(f - last_frames) * 10000000u / (now - last));
        hdmi_fps = (uint32_t)((uint64_t)(h - last_hdmi) * 1000000u / (now - last));
        last = now;
        last_frames = f;
        last_hdmi = h;
        if (stat_on) cmd_fps(0, NULL);
    }
    if (memlog_on && now - memlog_last >= 10000000) {
        memlog_last = now;
        memlog_report();
    }
}

void frank_quit(void) {
    frank_reboot();
}

/* Core 1, after the scanout started: the DAC and the OPL live there. */
static void core1_audio_init(void) {
    audio_init();
    audio_set_rate(OPL_WAR_RATE);
    opl_war_init();
}

/* ---- Boot: SD card, game data, errors ---- */

static bool data_present(void) {
    FILINFO fi;
    return f_stat(DATA_DIR "/DATA.WAR", &fi) == FR_OK && fi.fsize > 1000000;
}

static bool any_key(void) {
    uint8_t code;
    int down;
    usbhid_task();
    console_set_commands(cmds);
    console_service();
    while (usbhid_get_key_action(&code, &down))
        if (down) return true;
    return false;
}

static void say_where_data_goes(void) {
    shell_printf("Copy DATA.WAR from Warcraft: Orcs & Humans (DOS, retail or the shareware demo) to the card:\n");
    shell_highlight("  " DATA_DIR "/DATA.WAR\n");
}

static void wait_for_sd(void) {
    if (sd_mount()) return;
    shell_error("NO SD CARD", "Waiting for the card...");
    shell_printf("Insert a FAT32 or exFAT microSD card.\n");
    say_where_data_goes();
    while (!sd_mount()) {
        const uint64_t until = time_us_64() + 1000000;
        while (time_us_64() < until) any_key();
    }
    shell_welcome();
    shell_printf("SD card mounted.\n");
}

static void wait_for_data(void) {
    if (data_present()) return;
    shell_error("NO GAME DATA", "Waiting for the file...");
    say_where_data_goes();
    /* The card may be swapped meanwhile: mount again each time. */
    for (;;) {
        const uint64_t until = time_us_64() + 2000000;
        while (time_us_64() < until) any_key();
        if (sd_mount() && data_present()) break;
    }
    shell_welcome();
    shell_printf("Game data found.\n");
}

/* exit(): a game error or the game's own exit. */
void __attribute__((noreturn)) _exit(int status) {
    if (status != 0) {
        shell_error("GAME ERROR", "Press any key to restart");
        shell_errorf("The game stopped (status %d). See the UART console.\n", status);
        while (!any_key()) tight_loop_contents();
    }
    frank_reboot();
}

/* assert() in the game: its message on the error screen. */
void __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("assert: %s:%d %s: %s\n", file, line, func ? func : "", expr);
    shell_error("GAME ERROR", "Press any key to restart");
    const char *base = strrchr(file, '/');
    shell_errorf("%s:%d\n%s\n", base ? base + 1 : file, line, expr);
    while (!any_key()) tight_loop_contents();
    frank_reboot();
}

/* A CPU fault on core 0: the faulting PC on the UART and the error screen,
 * then a restart. */
void __attribute__((used)) frank_fault_report(uint32_t *frame, uint32_t exc_return) {
    const uint32_t cfsr = *(io_ro_32 *)0xE000ED28, hfsr = *(io_ro_32 *)0xE000ED2C;
    const uint32_t mmfar = *(io_ro_32 *)0xE000ED34, bfar = *(io_ro_32 *)0xE000ED38;
    printf("\nHARD FAULT core %u: pc %08lx lr %08lx sp %08lx | r0 %08lx r1 %08lx r2 %08lx r3 %08lx r12 %08lx\n"
           "  cfsr %08lx hfsr %08lx mmfar %08lx bfar %08lx exc_return %08lx\n",
           get_core_num(), (unsigned long)frame[6], (unsigned long)frame[5], (unsigned long)frame,
           (unsigned long)frame[0], (unsigned long)frame[1], (unsigned long)frame[2],
           (unsigned long)frame[3], (unsigned long)frame[4], (unsigned long)cfsr,
           (unsigned long)hfsr, (unsigned long)mmfar, (unsigned long)bfar, (unsigned long)exc_return);
    /* The hardware watchdog restarts the board whatever state the fault
     * left it in (the timer or the console may be what broke). */
    watchdog_enable(16000, false);
    if (get_core_num() == 0) {
        shell_error("GAME ERROR", "Restarting in 15 seconds");
        shell_errorf("CPU fault at %08lx (cfsr %08lx, bfar %08lx).\nSee the UART console.\n",
                     (unsigned long)frame[6], (unsigned long)cfsr, (unsigned long)bfar);
        /* Raw UART polling: `bootsel` still works here (tools/flash.sh). */
        const char *want = "bootsel";
        int got = 0;
        for (uint32_t spin = 0; spin < 15u * 1000u * 1000u; spin++) {
            if (uart_is_readable(uart0)) {
                const char c = (char)uart_getc(uart0);
                got = c == want[got] ? got + 1 : (c == want[0] ? 1 : 0);
                if (!want[got]) {
                    multicore_reset_core1();
                    psram_reset_to_spi();
                    reset_usb_boot(0, 0);
                }
            }
            busy_wait_us_32(1);
        }
    }
    frank_reboot();
}

void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile(
        /* a stack overflow got us here: give the report the last 1 KB */
        "ldr r0, =__StackLimit\n"
        "msr msplim, r0\n"
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b frank_fault_report\n");
}

static void cmd_errtest(int argc, char **argv) {
    const char *what = argc > 1 ? argv[1] : "game";
    if (!strcmp(what, "sd")) {
        shell_error("NO SD CARD", "Waiting for the card...");
        shell_printf("Insert a FAT32 or exFAT microSD card.\n");
        say_where_data_goes();
    } else if (!strcmp(what, "data")) {
        shell_error("NO GAME DATA", "Waiting for the file...");
        say_where_data_goes();
    } else {
        shell_error("GAME ERROR", "Press any key to restart");
        shell_errorf("war_resources.c:601\nconfiguration->startEntities\n");
    }
    const uint64_t until = time_us_64() + 8000000;
    while (time_us_64() < until) tight_loop_contents();
    frank_reboot();
}

int main(void) {
    /* Core 0's stack ends at __StackLimit: the M33's stack limit register
     * turns an overflow into a fault (reported below) instead of silently
     * overwriting the end of .bss, as war1's deep call chains once did. */
    /* (1 KB above it: the fault handler needs a stack to report from) */
    extern char __StackLimit[];
    __asm volatile("msr msplim, %0" :: "r"(__StackLimit + 1024));
    stdio_init_all();
    printf("\n\nfrank-war1 " FRANK_WAR1_VERSION " on " FRANK_BOARD_NAME " (%s %s)\n", __DATE__, __TIME__);
    if (!clocks_ok) printf("WARNING: clock setup failed\n");
    frank_clocks_report();

    swd_park(3);
    if (!display_init(core1_audio_init, plat_audio_service)) printf("video: init failed\n");
    swd_park(4);
    hang_sampling_start();
    swd_park(5);
    shell_welcome();
    {
        char clocks[64];
        shell_clocks(clocks, sizeof(clocks));
        shell_printf("frank-war1 %s on FRANK %s, RP2350%s: %s.\n", FRANK_WAR1_VERSION, FRANK_BOARD_NAME,
                     (*(io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET) & 1) ? "A" : "B", clocks);
    }
    shell_printf("PSRAM: %lu KB free for the game.\n",
                 (unsigned long)((0x117F0000u - (uint32_t)__psram_bss_end) >> 10));
    usbhid_init();
    swd_park(6);
#if FRANK_USB_CONSOLE
    shell_printf("USB: the CDC console (development build).\n");
#else
    shell_printf("USB host started: keyboard and mouse.\n");
#endif
    wait_for_sd();
    wait_for_data();
    f_chdir(DATA_DIR);
    {
        FILINFO fi;
        f_stat("DATA.WAR", &fi);
        shell_printf("SD card: " DATA_DIR "/DATA.WAR, %lu KB.\n", (unsigned long)(fi.fsize >> 10));
    }
    settings_load();
    load_launch_cfg();
    swd_park(7);
    extern bool plat_replay_start(const char *path);
    extern void plat_replay_set_speed(uint32_t speed);
    bool replay = false;
    uint32_t replay_x = 1;
    if (watchdog_hw->scratch[0] == REPLAY_MAGIC) {
        watchdog_hw->scratch[0] = 0;
        if (watchdog_hw->scratch[1])
            snprintf(replay_path, sizeof(replay_path), DATA_DIR "/replay%lu.txt", (unsigned long)watchdog_hw->scratch[1]);
        replay_x = watchdog_hw->scratch[2] >= 1 && watchdog_hw->scratch[2] <= 100 ? watchdog_hw->scratch[2] : 1;
        replay = true;
    }
    static char *argv[12];
    int argc = 0;
    argv[argc++] = "war1";
    if (replay) plat_replay_set_speed(replay_x);
    if (replay && plat_replay_start(replay_path)) {
        shell_highlight("Replaying %s at %lux...\n", replay_path, (unsigned long)replay_x);
        /* a recording starts at the logo, like the host, or as its "#!" says */
        cfg_mission[0] = 0;
        argc += replay_launch_args(replay_path, argv + argc, 10);
        shell_end(2000);
    } else {
        /* frank-castle: a moment for F12 before the game takes the screen */
        shell_hint("Press F12 for settings");
        shell_highlight("Starting Warcraft...\n");
        const uint64_t until = time_us_64() + 3000000;
        while (time_us_64() < until && !settings_pending()) {
            uint8_t code;
            int down;
            usbhid_task();
            frank_service();
            while (usbhid_get_key_action(&code, &down))
                if (down && code == 0x45) settings_request();
        }
        if (settings_pending()) settings_run(false);
        shell_hint(NULL);
    }

    if (cfg_mission[0]) {
        argv[argc++] = "--mission";
        argv[argc++] = cfg_mission;
        if (cfg_race[0]) { argv[argc++] = "--race"; argv[argc++] = cfg_race; }
        if (cfg_map[0]) { argv[argc++] = "--map"; argv[argc++] = cfg_map; }
        if (cfg_seed[0]) { argv[argc++] = "--seed"; argv[argc++] = cfg_seed; }
    }
    argv[argc] = NULL;
    const int r = war_main(argc, argv);
    printf("game exited (%d)\n", r);
    if (r != 0) _exit(r);
    frank_reboot();
}
