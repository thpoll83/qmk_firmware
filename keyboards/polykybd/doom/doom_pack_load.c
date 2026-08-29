// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// DoomPack loader — the firmware half of the executable-pack split
// (doom/PACK_DESIGN.md; compiled only under POLYKYBD_DOOM_PACK). Validates
// the PlyX pack flashed at FW_DOOMPACK_SLOT_OFF and dispatches every
// doom_shim_* call through its export table. Refusal is always safe: the
// stub table below answers every call with a no-op, and doom_mode.c then
// runs the fire demo exactly like a missing WHX.
#include QMK_KEYBOARD_H

#include "doom_mode.h"
#include "doom_arena.h" // doom_arena_at (the pool carve handed to the pack)
#include "doom_pack_abi.h"

#include "base/fw_staging.h"
#include "polymod_crc32.h"

#ifdef FW_REQUIRE_SIGNATURE
// FW-9: the pack is executable code, so it gets the same Ed25519 gate as the
// firmware image — verified HERE, at load time, not at flash COMMIT (flash can
// be rewritten after a COMMIT succeeds, so a "was validated once" flag is not a
// control). The __has_include fallback bridges the vendored Monocypher's move
// into the polymod_monocypher module (#242) — drop it once that lands.
#    if defined(__has_include) && __has_include("monocypher-ed25519.h")
#        include "monocypher-ed25519.h"
#    else
#        include "base/crypto/monocypher-ed25519.h"
#    endif
#    include "base/fw_pubkey.h"
#endif

#include <string.h>

// FW-9: a 64-byte Ed25519 signature over (header || image) — the header too, or
// a signed image could be re-targeted by editing entry_off/ram_base around it —
// trails the image at slot + DOOM_PACK_HDR_SIZE + image_size. Emitted by
// tools/sign_doompack.py; release.yml signs the .plyx beside the .bin.
#define DOOM_PACK_SIG_SIZE 64u

// ── Stub table: every call safe before a pack is loaded ─────────────────────
static void     stub_void(void) {}
static void     stub_set_role(bool m) { (void)m; }
static bool     stub_false(void) { return false; }
static int      stub_int0(void) { return 0; }
static unsigned stub_uint0(void) { return 0; }
static int      stub_face_index(void) { return -1; }
static void     stub_compose_line(uint8_t *line, unsigned y) { (void)line; (void)y; }
static bool     stub_hud_stats(int *h, int *a, int *m) { (void)h; (void)a; (void)m; return false; }
static bool     stub_weapon_state(uint8_t *o, uint8_t *r) { (void)o; (void)r; return false; }
static bool     stub_glyph(uint8_t g, uint8_t *o, uint8_t *w, uint8_t *h) {
    (void)g; (void)o; (void)w; (void)h;
    return false;
}
static int  stub_menu_snapshot(uint16_t *i, int n, int *s) { (void)i; (void)n; (void)s; return 0; }
static bool stub_menu_key_tile(uint8_t vr, uint8_t vc, uint8_t *t, const uint8_t *l, bool a) {
    (void)vr; (void)vc; (void)t; (void)l; (void)a;
    return false;
}
static bool stub_face_oled(uint8_t *b, const uint8_t *l) { (void)b; (void)l; return false; }
static void stub_engine_main(void) {
    // Never launched (doom_engine_start refuses without a loaded pack) —
    // spin defensively rather than return into a dead core1 stack.
    for (;;) {
        __asm volatile("wfe");
    }
}

static volatile uint8_t  s_stub_progress;
static volatile uint32_t s_stub_snd_fire, s_stub_snd_world;

static const doom_pack_api_t s_stub_api = {
    .size            = sizeof(doom_pack_api_t),
    .version         = 0,
    .engine_main     = stub_engine_main,
    .set_role        = stub_set_role,
    .take_frame      = stub_false,
    .release_frame   = stub_void,
    .compose_begin   = stub_void,
    .compose_line    = stub_compose_line,
    .drain_core1_log = stub_void,
    .progress        = &s_stub_progress,
    .gametic         = stub_int0,
    .video_type      = stub_uint0,
    .hud_stats       = stub_hud_stats,
    .weapon_state    = stub_weapon_state,
    .attract_active  = stub_false,
    .quit_requested  = stub_false,
    .slave_view_live = stub_false,
    .slave_wants_map_key = stub_false,
    .mirror_engaged  = stub_false,
    .drone_map_live  = stub_false,
    .tallnum_glyph   = stub_glyph,
    .hufont_glyph    = stub_glyph,
    .menu_snapshot   = stub_menu_snapshot,
    .menu_key_tile   = stub_menu_key_tile,
    .face_index      = stub_face_index,
    .face_oled       = stub_face_oled,
    .snd_fire        = &s_stub_snd_fire,
    .snd_world       = &s_stub_snd_world,
};

// ── Firmware services handed to the pack ────────────────────────────────────
// putchar_ is lib/printf's output funnel (quantum/logging/print.c) — the
// pack's core0 prints land in the same HID console as everything else. The
// pack's core1 never calls this (its printf goes to the pack-internal relay
// ring, drained by drain_core1_log on core0).
extern void putchar_(char c);

static void fw_console_putc(char c) {
    putchar_(c);
}

static const doom_fw_api_t s_fw_api = {
    .size          = sizeof(doom_fw_api_t),
    .version       = 1,
    .arena_at      = doom_arena_at,
    .arena_zone    = doom_arena_zone,
    .pop_key_event = doom_pop_key_event,
    .console_putc  = fw_console_putc,
};

// ── Loader state ─────────────────────────────────────────────────────────────
static const doom_pack_api_t *s_api = &s_stub_api;
static uint32_t               s_arena_off;

const doom_pack_api_t *doom_pack(void) {
    return s_api;
}

bool doom_pack_loaded(void) {
    return s_api != &s_stub_api;
}

uint32_t doom_pack_arena_off(void) {
    return s_arena_off;
}

void doom_pack_unload(void) {
    s_api       = &s_stub_api;
    s_arena_off = 0;
}

// Validate the flashed pack against THIS build's pool and call its init.
// `pool`/`pool_size` are the live borrowed pool — the pack must have been
// linked against exactly this address range (PACK_DESIGN.md §4). Every
// refusal prints its reason once per attempt; the caller falls back to the
// fire demo.
bool doom_pack_load(uint8_t *pool, uint32_t pool_size) {
    doom_pack_unload();

    const uint8_t         *slot = (const uint8_t *)(XIP_BASE + FW_RESOURCE_OFFSET + FW_DOOMPACK_SLOT_OFF);
    const doom_pack_hdr_t *hdr  = (const doom_pack_hdr_t *)(const void *)slot;

    if (hdr->magic[0] != DOOM_PACK_MAGIC0 || hdr->magic[1] != DOOM_PACK_MAGIC1 ||
        hdr->magic[2] != DOOM_PACK_MAGIC2 || hdr->magic[3] != DOOM_PACK_MAGIC3) {
        printf("doom: no PlyX pack at %p\n", (const void *)slot);
        return false;
    }
    if (hdr->abi != DOOM_PACK_ABI) {
        printf("doom: pack ABI %lu != %u — refuse\n", (unsigned long)hdr->abi, DOOM_PACK_ABI);
        return false;
    }
    if (hdr->image_size > FW_DOOMPACK_SLOT_SIZE - sizeof(*hdr)) {
        printf("doom: pack size %lu overflows the slot — refuse\n", (unsigned long)hdr->image_size);
        return false;
    }
    if (hdr->ram_base != (uint32_t)(uintptr_t)pool || hdr->ram_size > pool_size ||
        hdr->arena_off >= hdr->ram_size) {
        // The pack was linked for a different firmware build (the pool
        // moved). Coupled-but-verified by design — re-flash a matching pack.
        printf("doom: pack RAM %08lx+%lu != pool %p+%lu — stale pack, refuse\n",
               (unsigned long)hdr->ram_base, (unsigned long)hdr->ram_size,
               (void *)pool, (unsigned long)pool_size);
        return false;
    }
    // crc32_1byte takes a uint16_t length — chain it over the ~230 KB image
    // (chunked continuation is exact: the seed round-trips through the
    // final/initial complement).
    uint32_t       crc       = 0;
    const uint8_t *body      = slot + sizeof(*hdr);
    uint32_t       remaining = hdr->image_size;
    while (remaining) {
        const uint16_t n = remaining > 0xFFFFu ? 0xFFFFu : (uint16_t)remaining;
        crc = crc32_1byte(body, n, crc);
        body += n;
        remaining -= n;
    }
    if (crc != hdr->image_crc) {
        printf("doom: pack CRC %08lx != %08lx — refuse\n",
               (unsigned long)crc, (unsigned long)hdr->image_crc);
        return false;
    }

#ifdef FW_REQUIRE_SIGNATURE
    // FW-9: authenticate before branching into the image. The CRC above is an
    // integrity check anyone crafting a pack satisfies; this is the authorship
    // check, over header + image so no signed field can be re-targeted. There
    // is deliberately NO on-keycap escape hatch here (unlike an unsigned
    // FIRMWARE image): the load runs at idle, when nobody is present to answer
    // a prompt — an unsigned pack is simply refused and the fire demo runs.
    if (hdr->image_size > FW_DOOMPACK_SLOT_SIZE - sizeof(*hdr) - DOOM_PACK_SIG_SIZE) {
        printf("doom: pack leaves no room for its signature — refuse\n");
        return false;
    }
    const uint8_t *sig = slot + sizeof(*hdr) + hdr->image_size;
    if (crypto_ed25519_check(sig, FW_SIGNING_PUBKEY, slot, sizeof(*hdr) + hdr->image_size) != 0) {
        // Erased flash reads 0xFF; a pre-signing pack ends at image_size — both
        // present as a blank signature. Distinguish for the log only: the
        // decision is the same refusal either way.
        bool blank = true;
        for (uint32_t i = 0; i < DOOM_PACK_SIG_SIZE; i++) {
            if (sig[i] != 0x00 && sig[i] != 0xFF) {
                blank = false;
                break;
            }
        }
        printf("doom: pack %s — refuse (FW-9: flash a release-signed .plyx)\n",
               blank ? "is unsigned" : "signature is INVALID");
        return false;
    }
#endif

    // Entry: image offset -> XIP address, Thumb bit set. init runs the pack
    // crt0 (.data copy + .bss zero inside the pool — virgin engine statics
    // AND .data every session, which the monolith's doom_shim_set_role reset
    // list only approximates) and returns the export table.
    doom_pack_init_fn init =
        (doom_pack_init_fn)(uintptr_t)((uint32_t)(uintptr_t)(slot + sizeof(*hdr)) + hdr->entry_off + 1u);
    const doom_pack_api_t *api = init(&s_fw_api);
    if (api == NULL || api->size < sizeof(doom_pack_api_t)) {
        printf("doom: pack init refused (api %p size %lu) — stub stays\n",
               (const void *)api, api ? (unsigned long)api->size : 0ul);
        return false;
    }
    s_api       = api;
    s_arena_off = hdr->arena_off;
    printf("doom: pack v%lu loaded (%lu B, arena_off %lu)\n",
           (unsigned long)hdr->version, (unsigned long)hdr->image_size,
           (unsigned long)s_arena_off);
    return true;
}
