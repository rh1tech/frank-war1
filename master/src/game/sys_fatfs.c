/*
 * frank-blood — newlib system calls for the game: files on the microSD
 * (FatFs) and the heap in PSRAM.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The game uses POSIX open/read/lseek and stdio; the SDK's weak stubs are
 * replaced here. Descriptors 0-2 stay the UART console. Relative paths
 * resolve from FatFs's current directory (/blood, set at start).
 *
 * The heap is the PSRAM after the statically placed .psram_bss: the game
 * allocates megabytes (tile and resource caches) and SRAM is for the frame
 * buffers, the hot tables and code.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pico/stdlib.h"

#include "ff.h"

#define FIRST_FD 3
#define MAX_FILES 16

/* PSRAM: SRAM is kept for the frame buffers (zeroed by frank_preinit). */
static FIL files[MAX_FILES] __attribute__((section(".psram_bss")));
static bool used[MAX_FILES];

/* Read accounting by file name (console `io`). */
#define IO_NAMES 32
static struct { char name[16]; uint32_t bytes, calls, us; } io_stat[IO_NAMES];
static int io_slot[MAX_FILES];

static int io_find(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    for (int i = 0; i < IO_NAMES; i++) {
        if (!io_stat[i].name[0]) { strncpy(io_stat[i].name, base, 15); return i; }
        if (!strncmp(io_stat[i].name, base, 15)) return i;
    }
    return -1;
}

volatile uint32_t frank_io_us;   /* all reads, for the demo benchmark */

void frank_io_report(void) {
    for (int i = 0; i < IO_NAMES && io_stat[i].name[0]; i++)
        printf("io %-15s %8lu bytes %6lu reads %7lu ms\n", io_stat[i].name,
               (unsigned long)io_stat[i].bytes, (unsigned long)io_stat[i].calls,
               (unsigned long)(io_stat[i].us / 1000u));
}

static FIL *fil(int fd) {
    fd -= FIRST_FD;
    return (fd >= 0 && fd < MAX_FILES && used[fd]) ? &files[fd] : NULL;
}

static int fr_errno(FRESULT r) {
    switch (r) {
    case FR_NO_FILE: case FR_NO_PATH: case FR_INVALID_NAME: return ENOENT;
    case FR_DENIED: case FR_WRITE_PROTECTED: return EACCES;
    case FR_EXIST: return EEXIST;
    case FR_TOO_MANY_OPEN_FILES: return EMFILE;
    default: return EIO;
    }
}

int _open(const char *path, int flags, ...) {
    int i;
    for (i = 0; i < MAX_FILES && used[i]; i++) {}
    if (i == MAX_FILES) { errno = EMFILE; return -1; }
    BYTE mode;
    switch (flags & O_ACCMODE) {
    case O_WRONLY: mode = FA_WRITE; break;
    case O_RDWR: mode = FA_READ | FA_WRITE; break;
    default: mode = FA_READ; break;
    }
    if (flags & O_CREAT) mode |= (flags & O_TRUNC) ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    else if (flags & O_TRUNC) mode |= FA_CREATE_ALWAYS;
    if (flags & O_APPEND) mode |= FA_OPEN_APPEND;
    FRESULT r = f_open(&files[i], path, mode);
    if (r != FR_OK) { errno = fr_errno(r); return -1; }
    used[i] = true;
    io_slot[i] = io_find(path);
    return i + FIRST_FD;
}

int _close(int fd) {
    FIL *f = fil(fd);
    if (!f) return fd < FIRST_FD ? 0 : (errno = EBADF, -1);
    f_close(f);
    used[fd - FIRST_FD] = false;
    return 0;
}


int _read(int fd, char *buf, int len) {
    FIL *f = fil(fd);
    if (!f) {
        if (fd == 0) return 0;  /* the game never reads stdin */
        errno = EBADF;
        return -1;
    }
    UINT got;
    const uint32_t t0 = time_us_32();
    FRESULT r = f_read(f, buf, (UINT)len, &got);
    const int slot = io_slot[fd - FIRST_FD];
    if (slot >= 0) {
        io_stat[slot].bytes += got;
        io_stat[slot].calls++;
        io_stat[slot].us += time_us_32() - t0;
    }
    frank_io_us += time_us_32() - t0;
    if (r != FR_OK) { errno = fr_errno(r); return -1; }
    return (int)got;
}

int _write(int fd, char *buf, int len) {
    if (fd == 1 || fd == 2) {
        for (int i = 0; i < len; i++) {
            if (buf[i] == '\n') putchar_raw('\r');
            putchar_raw(buf[i]);
        }
        return len;
    }
    FIL *f = fil(fd);
    if (!f) { errno = EBADF; return -1; }
    UINT put;
    FRESULT r = f_write(f, buf, (UINT)len, &put);
    if (r != FR_OK) { errno = fr_errno(r); return -1; }
    return (int)put;
}

off_t _lseek(int fd, off_t pos, int whence) {
    FIL *f = fil(fd);
    if (!f) { errno = EBADF; return -1; }
    FSIZE_t to;
    switch (whence) {
    case SEEK_CUR: to = f_tell(f) + pos; break;
    case SEEK_END: to = f_size(f) + pos; break;
    default: to = (FSIZE_t)pos; break;
    }
    FRESULT r = f_lseek(f, to);
    if (r != FR_OK) { errno = fr_errno(r); return -1; }
    return (off_t)f_tell(f);
}

int _fstat(int fd, struct stat *st) {
    memset(st, 0, sizeof(*st));
    FIL *f = fil(fd);
    if (!f) {
        st->st_mode = S_IFCHR;
        return fd < FIRST_FD ? 0 : (errno = EBADF, -1);
    }
    st->st_mode = S_IFREG | 0644;
    st->st_size = (off_t)f_size(f);
    return 0;
}

int _stat(const char *path, struct stat *st) {
    FILINFO fi;
    memset(st, 0, sizeof(*st));
    FRESULT r = f_stat(path, &fi);
    if (r != FR_OK) { errno = fr_errno(r); return -1; }
    st->st_mode = (fi.fattrib & AM_DIR) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_size = (off_t)fi.fsize;
    return 0;
}

int stat(const char *path, struct stat *st) { return _stat(path, st); }

int _isatty(int fd) { return fd < FIRST_FD; }

int _unlink(const char *path) {
    FRESULT r = f_unlink(path);
    if (r != FR_OK) { errno = fr_errno(r); return -1; }
    return 0;
}

int access(const char *path, int mode) {
    FILINFO fi;
    if (f_stat(path, &fi) != FR_OK) { errno = ENOENT; return -1; }
    return 0;
}

int mkdir(const char *path, mode_t mode) {
    FRESULT r = f_mkdir(path);
    if (r != FR_OK && r != FR_EXIST) { errno = fr_errno(r); return -1; }
    return 0;
}

int rename(const char *from, const char *to) {
    f_unlink(to);
    FRESULT r = f_rename(from, to);
    if (r != FR_OK) { errno = fr_errno(r); return -1; }
    return 0;
}

char *getcwd(char *buf, size_t size) {
    if (f_getcwd(buf, (UINT)size) != FR_OK) return NULL;
    return buf;
}

int chdir(const char *path) {
    return f_chdir(path) == FR_OK ? 0 : (errno = ENOENT, -1);
}

/* ---- Heap: PSRAM after .psram_bss, up to the staging area ---- */
extern char __psram_heap_start[];
/* The last 64 KB of PSRAM stay free: tools/sd_put.sh stages files there over
 * SWD for the console's `sdput`. */
#define PSRAM_END ((char *)0x117F0000)

static char *heap_top;

void *_sbrk(int incr) {
    if (!heap_top) heap_top = __psram_heap_start;
    char *prev = heap_top;
    if (heap_top + incr > PSRAM_END || heap_top + incr < __psram_heap_start) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_top += incr;
    return prev;
}

/* PSRAM not yet handed to malloc (mallinfo only counts what sbrk gave it). */
uint32_t frank_heap_unclaimed(void) {
    return (uint32_t)(PSRAM_END - (heap_top ? heap_top : __psram_heap_start));
}

uint32_t frank_heap_used(void) {
    return heap_top ? (uint32_t)(heap_top - __psram_heap_start) : 0;
}

/* ---- Directories (the game's <dirent.h>: src/game/sysinc) ---- */
struct frank_dir {
    DIR dir;
    struct { unsigned char d_type; char d_name[256]; } ent;   /* = struct dirent */
};

void *opendir(const char *path) {
    struct frank_dir *d = malloc(sizeof(*d));
    if (!d) { errno = ENOMEM; return NULL; }
    if (f_opendir(&d->dir, path) != FR_OK) { free(d); errno = ENOENT; return NULL; }
    return d;
}

void *readdir(void *p) {
    struct frank_dir *d = p;
    FILINFO fi;
    if (f_readdir(&d->dir, &fi) != FR_OK || !fi.fname[0]) return NULL;
    d->ent.d_type = (fi.fattrib & AM_DIR) ? 4 : 8;   /* DT_DIR : DT_REG */
    strncpy(d->ent.d_name, fi.fname, sizeof(d->ent.d_name) - 1);
    d->ent.d_name[sizeof(d->ent.d_name) - 1] = 0;
    return &d->ent;
}

int closedir(void *p) {
    struct frank_dir *d = p;
    f_closedir(&d->dir);
    free(d);
    return 0;
}
