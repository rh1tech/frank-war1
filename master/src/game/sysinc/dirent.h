/*
 * frank-blood — <dirent.h> for the game (newlib has none), over FatFs.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_DIRENT_H
#define FRANK_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10

struct dirent {
    unsigned char d_type;
    char d_name[256];
};

typedef struct frank_dir DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif
