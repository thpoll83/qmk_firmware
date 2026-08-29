// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define HID_CMD_IDX 1
#define HID_DATA_IDX 2
// LANG_TO_UI32 / LANG_TO_UI32_ARR moved to lang/lang_lut.h — they are the
// lang-code encoding, and keeping them here made the generated lang_lut.c
// include the HID layer for one macro.

enum legacy_command_id {
    id_get_protocol_version                 = 0x01, // always 0x01
    id_get_keyboard_value                   = 0x02,
    id_set_keyboard_value                   = 0x03,
    id_dynamic_keymap_get_keycode           = 0x04,
    id_dynamic_keymap_set_keycode           = 0x05,
    id_dynamic_keymap_reset                 = 0x06,
    id_custom_set_value                     = 0x07,
    id_custom_get_value                     = 0x08,
    id_custom_save                          = 0x09,
    id_eeprom_reset                         = 0x0A,
    id_bootloader_jump                      = 0x0B,
    id_dynamic_keymap_macro_get_count       = 0x0C,
    id_dynamic_keymap_macro_get_buffer_size = 0x0D,
    id_dynamic_keymap_macro_get_buffer      = 0x0E,
    id_dynamic_keymap_macro_set_buffer      = 0x0F,
    id_dynamic_keymap_macro_reset           = 0x10,
    id_dynamic_keymap_get_layer_count       = 0x11,
    id_dynamic_keymap_get_buffer            = 0x12,
    id_dynamic_keymap_set_buffer            = 0x13,
    id_dynamic_keymap_get_encoder           = 0x14,
    id_dynamic_keymap_set_encoder           = 0x15,
    id_unhandled                            = 0xFF,
};

// Re-raise the GET_ID "fresh boot" marker (the '*' the host reads as "firmware
// just restarted"). The host's next reconnect probe pops it, resets its overlay
// MRU cache and re-pushes the current app's overlays. Used when the firmware has
// silently wiped its overlay memory mid-session (the doom easter egg borrows the
// overlay arena as game RAM and hands it back blank on exit), so the host — which
// otherwise only re-pushes on an app switch / reconnect — learns to resend.
void poly_mark_fresh_boot(void);
