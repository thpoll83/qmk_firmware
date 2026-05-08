#pragma once

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
    RESERVED_2          = 1 << 3,
    RESERVED_3          = 1 << 4,
    RESET_BUFFERS       = 1 << 5,
    USAGE_RESET         = 1 << 6,
    MAPPING_RESET       = 1 << 7
};

// Bits that trigger an overlay self-action (reset/set). They are processed
// immediately at HID receive (master) and bridge sync (slave) and cleared
// in local_state right there — housekeeping never has to deal with them.
#define OVERLAY_ACTION_FLAGS  ((uint8_t)(RESET_BUFFERS | USAGE_RESET | MAPPING_RESET | MAPPING_ALLSET))

bool test_flag(uint8_t flags, uint8_t f) ;

uint8_t flag_on(uint8_t flags, uint8_t f) ;

uint8_t flag_off(uint8_t flags, uint8_t f);

uint8_t toggle_flag(uint8_t flags, uint8_t f);

uint8_t set_flag(uint8_t flags, uint8_t f, bool set);

bool has_flag_changed(uint8_t flags1, uint8_t flags2, uint8_t f);

bool flag_turned_off(uint8_t flags1, uint8_t flags2, uint8_t f);

bool flag_turned_on(uint8_t flags1, uint8_t flags2, uint8_t f);
