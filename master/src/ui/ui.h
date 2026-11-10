/*
 * frank-war1 — drawing for the firmware's own screens (the boot shell, the
 * settings window) in Warcraft's look: RGB565 into a 320x200 frame, the
 * game's two pixel fonts (built into the firmware, port/war_fonts.h), the
 * menu's dark blue marble, and its stone-framed buttons.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_UI_H
#define FRANK_UI_H

#include <stdbool.h>
#include <stdint.h>

#define UI_W 320
#define UI_H 200

#define UI_RGB(r, g, b) ((uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3))

/* Warcraft's menu colours (sampled from its main menu). */
#define UI_CREAM    UI_RGB(239, 255, 219)   /* button text (FONT_NORMAL_COLOR) */
#define UI_GOLD     UI_RGB(255, 227, 73)    /* hotkey letter (FONT_HIGHLIGHT_COLOR) */
#define UI_GOLD_DK  UI_RGB(198, 130, 57)
#define UI_STONE    UI_RGB(189, 190, 189)   /* button frame, two tones */
#define UI_STONE2   UI_RGB(165, 186, 165)
#define UI_EDGE     UI_RGB(82, 97, 90)      /* line around the frame */
#define UI_SHADE    UI_RGB(41, 56, 49)      /* inside the frame */
#define UI_NAVY     UI_RGB(16, 48, 74)      /* button face */
#define UI_NAVY_HI  UI_RGB(33, 76, 115)     /* the selected button's face */
#define UI_INK      UI_RGB(0, 8, 24)        /* glyph outlines */
#define UI_DIM      UI_RGB(140, 160, 150)
#define UI_RED      UI_RGB(255, 96, 64)
#define UI_BLOOD    UI_RGB(99, 16, 8)

enum { UI_FONT_SMALL = 0, UI_FONT_MENU = 1 };

/* The frame drawn into. */
void ui_target(uint16_t *fb);
uint16_t *ui_fb(void);

void ui_fill(int x0, int y0, int x1, int y1, uint16_t c);
/* Darken to 3/8 (a modal window's backdrop): from `src` into the target. */
void ui_dim_copy(const uint16_t *src);
/* The menu's dark blue marble over a rectangle (the same pattern wherever
 * it is drawn, so pieces can be redrawn). */
void ui_marble(int x0, int y0, int x1, int y1);
/* Give back the marble's memory (the game is about to have the screen). */
void ui_release(void);
/* A button-style box: an edge line, a two-tone stone frame, a shade line,
 * then the face (`face`; 0 leaves the inside alone). `gold`: the frame in
 * gold (the selected item). */
void ui_box(int x0, int y0, int x1, int y1, uint16_t face, bool gold);
/* A window: a thick double stone frame around a marble face. */
void ui_window(int x0, int y0, int x1, int y1);

/* Text in one of the game's fonts: glyph pixels in `c`, their outline in
 * UI_INK; `hot` >= 0 draws that character in UI_GOLD. Returns the width. */
int ui_text(int font, int x, int y, const char *s, uint16_t c, int hot);
int ui_text_width(int font, const char *s);
int ui_text_height(int font);
void ui_text_center(int font, int cx, int y, const char *s, uint16_t c, int hot);
/* Letter-spaced capitals ("O R C S  &  H U M A N S"). */
void ui_text_spaced(int font, int cx, int y, const char *s, uint16_t c, int extra);
/* The WARCRAFT logo: the menu font scaled up in the logo's gold, the
 * capitals W and C larger, as on the game's title. Centred on cx. */
void ui_logo(int cx, int y);

#endif
