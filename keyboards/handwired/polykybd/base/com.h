#pragma once

#include <stdint.h>
#include <stdbool.h>

enum poly_flag {
    STATUS_DISP_ON      = 1 << 0,
    IDLE_TRANSITION     = 1 << 1,
    DISP_IDLE           = 1 << 2,
    DEAD_KEY_ON_WAKEUP  = 1 << 3,

    DBG_ON              = 1 << 4,
    MODS_AS_TEXT        = 1 << 5,
    MORE_TEXT           = 1 << 6,
    RGB_ON              = 1 << 7
};

enum overlay_flag {
    DISPLAY_OVERLAYS    = 1 << 0,
    MAPPING_ALLSET      = 1 << 1,
    // While set, every overlay upload is stored on both halves regardless of
    // the upload's keycode side. The host turns this on before MRU sends so a
    // mapping that crosses the split can find the bitmap on the side that
    // actually has to render it. Off = legacy side-conditional storage.
    MIRROR_OVERLAYS     = 1 << 2,
    // Set by master in the QK_BOOTLOADER case and force-synced via
    // USER_SYNC_POLY_DATA before reset_keyboard() runs. Slave detects the
    // 0->1 transition in user_sync_poly_data_handler() and renders the
    // "BOOT-LOADER!" message + solid red RGB so the slave half stays lit
    // while the master is in the RP2040 ROM bootloader.
    BOOTLOADER_DISPLAY  = 1 << 3,
    // Set by master when the host requests FW_UP_APPLY (cmd 0x44, the
    // irreversible "copy staged image to flash + reboot" step) and force-synced
    // via USER_SYNC_POLY_DATA before the apply runs. While set, both halves lock
    // the key matrix (no keystrokes reach the host so a stray key can't interrupt
    // the apply), force the RGB matrix to a solid blue-green, and show an
    // "APPLY/WAIT" message — mirroring BOOTLOADER_DISPLAY. The transfer/staging
    // phase (BEGIN/CHUNK/COMMIT) is CRC32-verified and deliberately NOT locked:
    // typing during it is harmless and a corrupt transfer is simply rejected.
    FW_APPLY_DISPLAY    = 1 << 4,
    RESET_BUFFERS       = 1 << 5,
    USAGE_RESET         = 1 << 6,
    MAPPING_RESET       = 1 << 7
};

// Bits that trigger an overlay self-action (reset/set). They are processed
// immediately at HID receive (master) and bridge sync (slave) and cleared
// in local_state right there — housekeeping never has to deal with them.
#define OVERLAY_ACTION_FLAGS  ((uint8_t)(RESET_BUFFERS | USAGE_RESET | MAPPING_RESET | MAPPING_ALLSET))

// State-flag bits that affect slave-side decision-making and therefore must
// be force-synced from master to slave at the moment the host changes them,
// not deferred to the next housekeeping cycle.
// MIRROR_OVERLAYS: upload bridge handlers use it immediately to decide whether
//   set_overlay_usage_post_upload is a no-op — must arrive before any upload.
// DISPLAY_OVERLAYS: adding it here makes enable_overlays() force-sync to slave
//   via case 11, triggering a state_diff refresh on slave after all mapping
//   chunks are guaranteed delivered — fixes ESC (and any key in a later chunk)
//   not appearing until a modifier change.
#define OVERLAY_SYNCED_STATE_FLAGS  ((uint8_t)(MIRROR_OVERLAYS | DISPLAY_OVERLAYS))

bool test_flag(uint8_t flags, uint8_t f) ;

uint8_t flag_on(uint8_t flags, uint8_t f) ;

uint8_t flag_off(uint8_t flags, uint8_t f);

uint8_t toggle_flag(uint8_t flags, uint8_t f);

uint8_t set_flag(uint8_t flags, uint8_t f, bool set);

bool has_flag_changed(uint8_t flags1, uint8_t flags2, uint8_t f);

bool flag_turned_off(uint8_t flags1, uint8_t flags2, uint8_t f);

bool flag_turned_on(uint8_t flags1, uint8_t flags2, uint8_t f);
