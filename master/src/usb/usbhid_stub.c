/*
 * frank-war1 — the USB HID API without a USB host (USB_HID=OFF: the native
 * USB is the CDC console, frank-castle's development build). No keyboard,
 * mouse or gamepad then; the console's `key` and `mouse` commands drive
 * the game.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>

#include "usbhid.h"

void usbhid_init(void) {}
void usbhid_task(void) {}
int usbhid_keyboard_connected(void) { return 0; }
int usbhid_mouse_connected(void) { return 0; }
void usbhid_get_keyboard_state(usbhid_keyboard_state_t *state) { memset(state, 0, sizeof(*state)); }
void usbhid_get_mouse_state(usbhid_mouse_state_t *state) { memset(state, 0, sizeof(*state)); }
int usbhid_get_key_action(uint8_t *keycode, int *down) { return 0; }
uint16_t usbhid_get_kbd_state(void) { return 0; }
int usbhid_ctrl_alt_del_pressed(void) { return 0; }
int usbhid_get_raw_char(void) { return -1; }
int usbhid_gamepad_connected(void) { return 0; }
void usbhid_get_gamepad_state(usbhid_gamepad_state_t *state) { memset(state, 0, sizeof(*state)); }
int usbhid_gamepad_connected_idx(int idx) { return 0; }
void usbhid_get_gamepad_state_idx(int idx, usbhid_gamepad_state_t *state) { memset(state, 0, sizeof(*state)); }

uint8_t usbhid_keycode_to_ascii(uint8_t code, uint8_t modifier) {
    const int shift = modifier & 0x22;
    if (code >= 0x04 && code <= 0x1d) return (uint8_t)((shift ? 'A' : 'a') + code - 0x04);
    if (code >= 0x1e && code <= 0x26) return (uint8_t)('1' + code - 0x1e);
    if (code == 0x27) return '0';
    if (code == 0x2c) return ' ';
    return 0;
}
