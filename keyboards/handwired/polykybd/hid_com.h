#pragma once

#define HID_CMD_IDX 1
#define HID_DATA_IDX 2
#define LANG_TO_UI32(a,b,c,d) (((uint32_t)(a))<<24 | ((uint32_t)(b))<<16 | ((uint32_t)(c))<<8 | (d))
#define LANG_TO_UI32_ARR(arr) (((uint32_t)(arr[0]))<<24 | ((uint32_t)(arr[1]))<<16 | ((uint32_t)(arr[2]))<<8 | (arr[3]))

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

#include <stdbool.h>

// --- HID firmware-apply safety mode (see hid_com.c for the full rationale) ---

// True while the irreversible FW_UP_APPLY lockout is engaged on this half.
// Used by process_record to drop all key events so a keystroke can't interrupt
// the apply.
bool is_fw_apply_active(void);

// Engage / release the apply lockout (matrix freeze + blue-green RGB +
// "APPLY/WAIT" on both halves). enter() force-syncs the flag to the slave.
void fw_apply_safety_enter(void);
void fw_apply_safety_exit(void);

// Watchdog: call from housekeeping_task_user() on both sides. Force-clears the
// lockout if it has been engaged longer than the apply could legitimately take
// (recovery net for builds with no apply backend / a backend that failed).
void fw_apply_safety_tick(void);

// Weak hook: the staged-image apply backend. Default is a no-op; a firmware
// with in-app apply overrides it (copies staging -> flash on both halves and
// resets — never returns).
void polykybd_apply_staged_image(void);
