#include "overlay.h"
#include "config.h" //for all constants

#include "disp_array.h"
#include "polymod_rle.h"

#include <print.h>
#include <string.h>

// Bit capacity of one keycap overlay row (72x40 = 2880 bits = 360 bytes). Single
// source of truth for the copy-loop "done"/overflow sentinel so the bounds stay
// correct if the display geometry ever changes.
#define OVERLAY_BIT_CAPACITY (SCREEN_WIDTH * SCREEN_HEIGHT)

static volatile uint8_t use_overlay[(NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP/8)+1];
// ⚠️ overlays[] IS the doom easter-egg's entire game arena (borrowed as RAM). Its
// size = NUM_OVERLAYS*NUM_VARIATIONS*360 = 226,800 B today. If you SHRINK the pool
// to reclaim RAM, keep it >= ~205 KB or the doom build won't fit: the engine floor
// is ~144 KB of fixed structure (frame buffer 53,760 + pd_render 58,880 + statics
// 20,824 + vpatch/compose/mirror/stack) plus a >=~58 KB zone heap. The monolith
// link fails loudly if it overflows (.doom_shared is pool-sized), but the DoomPack
// pins its statics at this array's address, so verify both flavours. See
// doom/doom_arena.h + doom/README.md "Engine integration state" for the breakdown.
#if defined(POLYKYBD_DOOM) && !defined(POLYKYBD_DOOM_PACK)
// Doom dev builds: the pool is the linker's .doom_shared block, shared with
// the game engine's zero-init statics (keyboards/polykybd/ld/
// RP2040_FLASH_TIMECRIT_DOOM.ld) — game mode and overlay use are mutually
// exclusive, and the block is exactly pool-sized. NOT zeroed by crt0: the
// usage bits above gate every read, and reset/refill paths memset it.
// (The DoomPack flavour keeps the plain array below: the flashed pack links
// its statics AT the array's measured address instead — doom/PACK_DESIGN.md.)
extern uint8_t __doom_shared_base__[];
#define overlays ((uint8_t (*)[72*40/8])__doom_shared_base__)
#define OVERLAYS_SIZE (NUM_OVERLAYS*NUM_VARIATIONS*(72*40/8))
#elif defined(POLYKYBD_DOOM_PACK)
// DoomPack flavour: the pool is PINNED at the RAM origin (0x20000000) via
// the dedicated .overlay_pool section (ld/RP2040_FLASH_TIMECRIT_DOOMPACK.ld)
// so the flashed engine pack's RAM base is a build-independent constant.
// Like the monolith's .doom_shared the section is NOLOAD (crt0 does not
// zero it) — the usage bits above gate every read and the reset/refill
// paths memset it, so boot behaviour is unchanged.
static uint8_t overlays [NUM_OVERLAYS*NUM_VARIATIONS][72*40/8] __attribute__((section(".overlay_pool")));
#define OVERLAYS_SIZE sizeof(overlays)
#else
static /*volatile*/ uint8_t overlays [NUM_OVERLAYS*NUM_VARIATIONS][72*40/8]; // ResX*ResY/PixelPerByte
#define OVERLAYS_SIZE sizeof(overlays)
#endif
static uint16_t overlay_map [NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP];

static overlay_fragment_context_t g_fragment_context = {0};


const overlay_fragment_context_t* get_fragment_context(void) {
    return &g_fragment_context;
}

overlay_fragment_context_t* access_fragment_context(void) {
    return &g_fragment_context;
}

void reset_fragment_context(void) {
    memset(&g_fragment_context, 0, sizeof(overlay_fragment_context_t));
}

roi_bounds_t set_fragment_context_from_buffer(const uint8_t *buffer) {
    g_fragment_context.msg_count= 0;
    g_fragment_context.bit_index = 0;
    g_fragment_context.keycode = buffer[0];
    g_fragment_context.modifier = buffer[1]&0x0f;
    g_fragment_context.roi.y = (buffer[2]&0x03) | ((buffer[1]>>2)&0x3c);
    g_fragment_context.roi.yy = buffer[2] >> 2;
    g_fragment_context.roi.x = buffer[3];
    g_fragment_context.roi.xx = buffer[4]&0x7f;
    g_fragment_context.roi.compressed = ((buffer[4]&0x80)!=0);
    // SECURITY: the ROI bounds are raw host bytes (y/yy up to 63, x up to 255,
    // xx up to 127) but the keycap is only 72x40. Unclamped, copy_rectangle_to_overlay_xy
    // would index a 360-byte overlay row well past its end (OOB write into adjacent
    // overlays[]). Clip to the visible window so x/y are valid start coords and
    // xx/yy are valid exclusive ends. (A second backstop lives in the copy loop.)
    // NOTE: x/y are INCLUSIVE start pixels (max SCREEN_-1) while xx/yy are
    // EXCLUSIVE ends (max SCREEN_); width = xx-x, so a 1px ROI (xx==x+1) is valid
    // and x==xx is an empty (no-write) region — this clamp imposes no min size.
    bool clamped = false;
    if (g_fragment_context.roi.xx > SCREEN_WIDTH)   { g_fragment_context.roi.xx = SCREEN_WIDTH;      clamped = true; }
    if (g_fragment_context.roi.yy > SCREEN_HEIGHT)  { g_fragment_context.roi.yy = SCREEN_HEIGHT;     clamped = true; }
    if (g_fragment_context.roi.x  >= SCREEN_WIDTH)  { g_fragment_context.roi.x  = SCREEN_WIDTH  - 1; clamped = true; }
    if (g_fragment_context.roi.y  >= SCREEN_HEIGHT) { g_fragment_context.roi.y  = SCREEN_HEIGHT - 1; clamped = true; }
    // Enforce the x<=xx / y<=yy invariant: an inverted/empty range from malformed
    // host input must produce no write (degenerate ROI), never an underflowing loop.
    if (g_fragment_context.roi.x > g_fragment_context.roi.xx) { g_fragment_context.roi.x = g_fragment_context.roi.xx; clamped = true; }
    if (g_fragment_context.roi.y > g_fragment_context.roi.yy) { g_fragment_context.roi.y = g_fragment_context.roi.yy; clamped = true; }
    // Report clamping so the HID layer can log a misbehaving/hostile host instead
    // of silently swallowing out-of-bounds ROI bounds.
    return clamped ? ROI_BOUNDS_CLAMPED : ROI_BOUNDS_OK;
}

void set_fragment_context_key(uint8_t keycode, uint8_t modifier) {
    g_fragment_context.keycode = keycode;
    g_fragment_context.modifier = modifier;
}

void set_fragment_context_bit_index(uint16_t bit_index) {
    g_fragment_context.bit_index = bit_index;
}

void set_fragment_context_byte_len(uint8_t byte_len) {
    g_fragment_context.byte_len = byte_len;
}

void set_fragment_context_msg_count(uint8_t msg_count) {
    g_fragment_context.msg_count = msg_count;
}

void set_fragment_context_roi(uint8_t x, uint8_t y, uint8_t xx, uint8_t yy, bool compressed) {
    g_fragment_context.roi.x = x;
    g_fragment_context.roi.y = y;
    g_fragment_context.roi.xx = xx;
    g_fragment_context.roi.yy = yy;
    g_fragment_context.roi.compressed = compressed;
}


uint8_t (* get_overlays(void))[72*40/8] {
    return overlays;
}

// Throwaway row returned for an out-of-range overlay index — so an OOB access
// neither reads past the pool nor corrupts a real slot. Writes land here and are
// dropped; reads come back blank instead of slot 0's legitimate keycap image.
static uint8_t s_overlay_discard[72*40/8];

uint8_t* get_overlay(uint16_t overlay_idx) {
    // SECURITY: this is the single place overlays[] is dereferenced. The index
    // arrives from the host-programmed mapping (get_overlay_mapping -> overlay_map[],
    // cmd 21) and is only validated at write time today, so an out-of-range value
    // here would read/write past the pool. Route it to a discard row instead —
    // memory-safe AND non-destructive (no real slot is read or overwritten; slot 0
    // would have been a legitimate keycap image). Logged once so a stale/buggy
    // mapping doesn't disappear silently, one-shot to avoid flooding the render path.
    if (overlay_idx >= NUM_OVERLAYS*NUM_VARIATIONS) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            uprintf("get_overlay: index %u out of range (max %u) — discarding.\n",
                    (unsigned)overlay_idx, (unsigned)(NUM_OVERLAYS*NUM_VARIATIONS - 1));
        }
        return s_overlay_discard;
    }
    return overlays[overlay_idx];
}

uint16_t get_overlay_mapping(uint16_t overlay_idx) {
    return overlay_map[overlay_idx];
}

void set_overlay_mapping(uint16_t overlay_idx, uint16_t val) {
    overlay_map[overlay_idx] = val;
}

void set_overlay_usage(uint16_t overlay_idx) {
    use_overlay[overlay_idx/8] |= (1<<(overlay_idx%8));
}

bool is_overlay_used(uint16_t overlay_idx) {
    return (use_overlay[overlay_idx/8] & (1<<(overlay_idx%8))) != 0;
}

void reset_overlay_buffers(void) {
    memset(overlays, 0, OVERLAYS_SIZE);
}

void reset_overlay_usage(void) {
    for(int16_t i = 0; i < sizeof(use_overlay); ++i) {
        use_overlay[i] = 0;
    }
}

void set_all_overlay_mapping(void) {
    for(int16_t i = 0; i < sizeof(use_overlay); ++i) {
        use_overlay[i] = 0xff;
    }
}

void reset_overlay_mapping(void) {
    for(int16_t i = 0; i < NUM_OVERLAYS*NUM_VARIATIONS; ++i) {
        overlay_map[i] = i;
    }
    //the additional map entries for Ctrl+Alt+Shift and GUI Modifiers will point to the no_modifier version (0-NUM_OVERLAYS)
    for(int16_t i = NUM_OVERLAYS*NUM_VARIATIONS; i < NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP; ++i) {
        overlay_map[i] = i%NUM_OVERLAYS;
    }
}

uint16_t copy_rectangle_to_overlay_xy(uint16_t bit_index, uint8_t* dest, const volatile uint8_t* data, const volatile roi_update_data_t* roi, const uint16_t bitlen) {
    uint16_t bit_cnt = 0;
    uint8_t  start_y = bit_index / SCREEN_WIDTH;
    for (uint8_t dest_y = start_y; dest_y < roi->yy; dest_y++) {
        uint8_t start_x = bit_cnt == 0 ? bit_index % SCREEN_WIDTH : roi->x;
        if (start_x == roi->xx) {
            start_x = roi->x;
            dest_y++;
            if (dest_y >= roi->yy) {
                return bit_index;
            }
        } else if(start_x < roi->x) {
            start_x = roi->x;
        }
        for (uint8_t dest_x = start_x; dest_x < roi->xx; dest_x++) {
            bit_index = dest_y * SCREEN_WIDTH + dest_x;
            // SECURITY backstop: dest is a single SCREEN_WIDTH*SCREEN_HEIGHT-bit
            // (360-byte) overlay row. Even if a caller hands us an out-of-range roi
            // (e.g. via the split bridge), never write past it — bail at the row
            // boundary, returning the geometry-derived "done" sentinel callers test.
            if (bit_index >= OVERLAY_BIT_CAPACITY) {
                return OVERLAY_BIT_CAPACITY;
            }
            uint8_t data_byte   = data[bit_cnt / 8];
            uint8_t bit_in_byte = bit_cnt % 8;
            bool    bit_is_set  = ((0x80 >> bit_in_byte) & data_byte) != 0;

            if (bit_is_set) {
                dest[bit_index / 8] |= 0x80 >> (bit_index % 8);
            } else {
                dest[bit_index / 8] &= ~(0x80 >> (bit_index % 8));
            }

            bit_cnt++;
            if (bit_cnt >= bitlen) {
                bit_index++; //for the next call
                return bit_index;
            }
        }
    }
    return OVERLAY_BIT_CAPACITY;
}

uint16_t copy_rectangle_to_overlay(uint16_t bit_index, uint8_t* dest, const volatile uint8_t* data, const volatile roi_update_data_t* roi, const uint8_t data_len) {
    if(roi->compressed) {
        for (uint8_t i = 0; i < data_len; i++) {
            uint8_t buffer[16];
            uint16_t num_bits = rle_decompress(buffer, 16, &data[i], 1, 0);
            bit_index = copy_rectangle_to_overlay_xy(bit_index, dest, buffer, roi, num_bits);
            if( bit_index >= OVERLAY_BIT_CAPACITY) {
                return bit_index;
            }
        }
    } else {
        bit_index = copy_rectangle_to_overlay_xy(bit_index, dest, data, roi, data_len*8);
    }
    return bit_index >= ((roi->yy-1) * SCREEN_WIDTH + roi->xx) ? OVERLAY_BIT_CAPACITY : bit_index;
}
