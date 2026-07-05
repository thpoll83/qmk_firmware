// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// DoomPack ABI — the contract between the shipping firmware and the
// executable engine pack flashed at FW_DOOMPACK_SLOT_OFF (see PACK_DESIGN.md;
// feasibility study "Option 2"). Compiled into BOTH sides: the firmware's
// loader/dispatch (doom_mode.c under POLYKYBD_DOOM_PACK) and the pack's init
// (pack build of qmk_shim.c). Everything here is versioned:
//
//  - DOOM_PACK_ABI gates the header (layout of doom_pack_hdr_t itself plus
//    everything reachable from it). Bump on ANY change to the structs below,
//    to the arena layout constants (DOOM_ARENA_*_OFF), or to the shared
//    doom_mirror_t mailbox in doom_mirror.h — the mailbox lives in the arena
//    and is accessed from both sides by layout, not through the tables.
//  - The two call tables lead with their byte size; each side refuses a
//    table smaller than the fields it needs, so APPENDING fields is
//    compatible without an ABI bump. Reordering/retyping is not.
//
// RAM model (PACK_DESIGN.md §4): the pack's .data/.bss are linked at the
// borrowed overlay pool's address of the EXACT firmware build it ships with
// (extracted from the .elf at pack-build time, recorded in ram_base/ram_size)
// — the loader compares them against the live pool and refuses a mismatched
// pack, falling back to the fire demo. Coupled but verified.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOOM_PACK_MAGIC0 'P'
#define DOOM_PACK_MAGIC1 'l'
#define DOOM_PACK_MAGIC2 'y'
#define DOOM_PACK_MAGIC3 'X'
#define DOOM_PACK_ABI 1u

// Little-endian, at the start of the DOOMPACK flash slot; the linked image
// (.text + .rodata + .data initializers) follows immediately.
typedef struct {
    char     magic[4];   // "PlyX"
    uint32_t abi;        // DOOM_PACK_ABI of the pack build
    uint32_t image_size; // bytes following this header
    uint32_t image_crc;  // CRC32 (crc32_1byte, seed 0) over the image bytes
    uint32_t entry_off;  // image offset of doom_pack_init (loader ORs the Thumb bit)
    uint32_t ram_base;   // pool address the pack was linked against
    uint32_t ram_size;   // pool bytes the pack claims (<= the real pool size)
    uint32_t version;    // content version (host staleness checks, GET_ID-style)
} doom_pack_hdr_t;

// Firmware services the pack imports (PACK_DESIGN.md §3). Kept deliberately
// tiny: libc/libgcc are linked INTO the pack, pico_sync/time_us_64 are
// hardware-only, and the mirror mailbox is shared arena memory — so only the
// arena carve, the input ring and the core0 console byte sink cross here.
typedef struct {
    uint32_t size;    // sizeof(doom_fw_api_t) the firmware was built with
    uint32_t version; // informational (firmware FW_VERSION-derived)
    uint8_t *(*arena_at)(unsigned offset);          // borrowed-pool carve
    uint8_t *(*arena_zone)(int *size);              // engine zone region
    bool     (*pop_key_event)(uint8_t *key, bool *pressed); // input ring
    void     (*console_putc)(char c);               // core0 printf sink
} doom_fw_api_t;

// Everything the firmware calls on the engine side (audited from the v43
// objects: the doom_shim_* surface of doom_mode.c/doom_blit.c, the core1
// entry, and the addresses of the three shared volatiles that can't cross a
// call table as symbols). Signatures mirror doom_mode.h one-to-one.
typedef struct {
    uint32_t size;    // sizeof(doom_pack_api_t) the pack was built with
    uint32_t version; // pack content version (== hdr.version)

    // Engine lifecycle: core1 entry (the D_DoomMain wrapper the firmware
    // hands to multicore_launch_core1_*), and the session role.
    void (*engine_main)(void);
    void (*set_role)(bool master);

    // Frame handoff + compose (pico_sync lives pack-side).
    bool (*take_frame)(void);
    void (*release_frame)(void);
    void (*compose_begin)(void);
    void (*compose_line)(uint8_t *line, unsigned y);

    // Core1 log relay + boot breadcrumbs.
    void (*drain_core1_log)(void);
    volatile uint8_t *progress;

    // Game-state peeks.
    int      (*gametic)(void);
    unsigned (*video_type)(void);
    bool     (*hud_stats)(int *health, int *armor, int *ammo);
    bool     (*weapon_state)(uint8_t *owned_mask, uint8_t *ready_slot);
    bool     (*attract_active)(void);
    bool     (*quit_requested)(void);

    // Slave mirror gates.
    bool (*slave_view_live)(void);
    bool (*slave_wants_map_key)(void);
    bool (*mirror_engaged)(void);
    bool (*drone_map_live)(void);

    // WHX artwork decodes.
    bool (*tallnum_glyph)(uint8_t glyph, uint8_t *out8bpp, uint8_t *w, uint8_t *h);
    bool (*hufont_glyph)(uint8_t ch, uint8_t *out8bpp, uint8_t *w, uint8_t *h);
    int  (*menu_snapshot)(uint16_t *items, int max_items, int *item_on);
    bool (*menu_key_tile)(uint8_t vr, uint8_t vc, uint8_t *tile360,
                          const uint8_t *luma256, bool skull_alt);
    int  (*face_index)(void);
    bool (*face_oled)(uint8_t *oled_buf, const uint8_t *luma256);

    // Sound->RGB counters (S_StartSound edges, read by doom_rgb_compute).
    volatile uint32_t *snd_fire;
    volatile uint32_t *snd_world;
} doom_pack_api_t;

// The pack's sole entry point (hdr.entry_off): runs the pack crt0 (.data
// copy + .bss zero inside the borrowed pool), stores `fw`, returns the
// export table — or NULL when it refuses `fw` (size too small / unknown
// shape), in which case the loader falls back to the fire demo.
typedef const doom_pack_api_t *(*doom_pack_init_fn)(const doom_fw_api_t *fw);

#ifdef __cplusplus
}
#endif
