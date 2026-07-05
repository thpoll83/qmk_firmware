// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// DoomPack pack-side init — compiled ONLY into the standalone pack link
// (doom/pack/build_pack.sh), never into any firmware image. Provides:
//   - doom_pack_init: the PlyX entry point (crt0 + fw-table stash + export
//     table), see doom_pack_abi.h;
//   - the pack-local definitions of everything qmk_shim.c/the engine import
//     from the firmware (arena carve, input ring, console sink) — thin
//     forwarders through the stashed doom_fw_api_t;
//   - the tiny runtime the pack objects expect but the firmware normally
//     supplies (panic / hard_assertion_failure / time_us_64 / the __real_*
//     halves of the monolith's --wrap set).
//
// The engine objects themselves are the UNMODIFIED .o files of a monolithic
// POLYKYBD_DOOM build, re-linked at the pack address (PACK_DESIGN.md §7).
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "../doom_pack_abi.h"
#include "../doom_mode.h" // doom_shim_* declarations (plain POLYKYBD_DOOM side)

// Content version stamped into the export table (the header's copy is
// written by mkpack.py from the same -D on the build command line).
#ifndef DOOM_PACK_VERSION
#define DOOM_PACK_VERSION 0
#endif

// ── crt0 symbols (doom/pack/pack.ld.in) ─────────────────────────────────────
extern uint8_t __pack_data_load__[];
extern uint8_t __pack_data_start__[];
extern uint8_t __pack_data_end__[];
extern uint8_t __pack_bss_start__[];
extern uint8_t __pack_bss_end__[];

// ── firmware services ────────────────────────────────────────────────────────
static const doom_fw_api_t *s_fw;

uint8_t *doom_arena_at(unsigned offset) {
    return s_fw->arena_at(offset);
}

uint8_t *doom_arena_zone(int *size) {
    return s_fw->arena_zone(size);
}

bool doom_pop_key_event(uint8_t *key, bool *pressed) {
    return s_fw->pop_key_event(key, pressed);
}

// qmk_shim.c's core-aware __wrap_putchar_ sends core0 bytes to
// __real_putchar_ — which the pack link's --wrap=putchar_ resolves to the
// PLAIN putchar_ symbol, i.e. this definition (in the monolith: the
// firmware console). Forwards to the firmware's console sink. Guarded:
// printf before init (impossible today — the firmware only calls the table
// after init) drops.
void putchar_(char c) {
    if (s_fw) {
        s_fw->console_putc(c);
    }
}

// The monolith wraps the allocator so core1 goes to the zone and core0 to
// the real newlib heap; the pack has no heap and no live core0 allocation
// call sites (nm-verified in the monolith — see rules.mk). These PLAIN
// definitions are what --wrap resolves __real_* to, and being object
// symbols they also pre-empt newlib's heap (an archive member never gets
// pulled) — allocation fails loudly-but-safely instead of faulting.
void *malloc(size_t n) {
    (void)n;
    return NULL;
}
void *calloc(size_t n, size_t m) {
    (void)n;
    (void)m;
    return NULL;
}
void *realloc(void *p, size_t n) {
    (void)p;
    (void)n;
    return NULL;
}
void free(void *p) {
    (void)p;
}
char *strdup(const char *s) {
    (void)s;
    return NULL;
}

// ── pico-sdk runtime bits the firmware normally provides ────────────────────
// panic/hard_assertion_failure: pico_sdk_shims.o equivalents (park the
// calling core; the engine treats both as fatal). time_us_64: the RP2040
// timer's latched 64-bit read (TIMELR then TIMEHR — LR latches HR).
void panic(const char *fmt, ...) {
    (void)fmt;
    for (;;) {
        __asm volatile("wfe");
    }
}

void hard_assertion_failure(void) {
    for (;;) {
        __asm volatile("wfe");
    }
}

#define PACK_TIMER_BASE 0x40054000u
uint64_t time_us_64(void) {
    volatile uint32_t *timelr = (volatile uint32_t *)(PACK_TIMER_BASE + 0x0Cu);
    volatile uint32_t *timehr = (volatile uint32_t *)(PACK_TIMER_BASE + 0x08u);
    const uint32_t     lo     = *timelr; // latches HR
    const uint32_t     hi     = *timehr;
    return ((uint64_t)hi << 32) | lo;
}

// ── the export table ─────────────────────────────────────────────────────────
extern void D_DoomMain(void);

static void pack_engine_main(void) {
    D_DoomMain(); // never returns
    for (;;) {
        __asm volatile("wfe");
    }
}

static const doom_pack_api_t s_pack_api = {
    .size            = sizeof(doom_pack_api_t),
    .version         = DOOM_PACK_VERSION,
    .engine_main     = pack_engine_main,
    .set_role        = doom_shim_set_role,
    .take_frame      = doom_shim_take_frame,
    .release_frame   = doom_shim_release_frame,
    .compose_begin   = doom_shim_compose_begin,
    .compose_line    = doom_shim_compose_line,
    .drain_core1_log = doom_shim_drain_core1_log,
    .progress        = &doom_shim_progress,
    .gametic         = doom_shim_gametic,
    .video_type      = doom_shim_video_type,
    .hud_stats       = doom_shim_hud_stats,
    .weapon_state    = doom_shim_weapon_state,
    .attract_active  = doom_shim_attract_active,
    .quit_requested  = doom_shim_quit_requested,
    .slave_view_live = doom_shim_slave_view_live,
    .slave_wants_map_key = doom_shim_slave_wants_map_key,
    .mirror_engaged  = doom_shim_mirror_engaged,
    .drone_map_live  = doom_shim_drone_map_live,
    .tallnum_glyph   = doom_shim_tallnum_glyph,
    .hufont_glyph    = doom_shim_hufont_glyph,
    .menu_snapshot   = doom_shim_menu_snapshot,
    .menu_key_tile   = doom_shim_menu_key_tile,
    .face_index      = doom_shim_face_index,
    .face_oled       = doom_shim_face_oled,
    .snd_fire        = &doom_shim_snd_fire,
    .snd_world       = &doom_shim_snd_world,
};

// The PlyX entry point (hdr.entry_off). Runs BEFORE the pack's own
// .data/.bss exist — nothing here may touch pack statics until the copy/
// zero below (s_fw is .bss: stashed after). Returning NULL refuses the
// firmware's table and leaves the loader on its stub.
__attribute__((section(".text.doom_pack_init"), used))
const doom_pack_api_t *doom_pack_init(const doom_fw_api_t *fw) {
    if (fw == NULL || fw->size < sizeof(doom_fw_api_t)) {
        return NULL;
    }
    memcpy(__pack_data_start__, __pack_data_load__,
           (size_t)(__pack_data_end__ - __pack_data_start__));
    memset(__pack_bss_start__, 0, (size_t)(__pack_bss_end__ - __pack_bss_start__));
    s_fw = fw;
    return &s_pack_api;
}
