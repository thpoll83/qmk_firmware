// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Layout of the borrowed overlay pool (226,800 B) while game mode runs. The
// keyboard's RAM is otherwise fully committed, so EVERYTHING the engine needs
// lives in the pool, in two tiers:
//
//  1. The engine's zero-init statics (.bss + vintage COMMON tentative
//     definitions, ~21 KB) — placed at the FRONT of the pool by the doom
//     linker script (.doom_shared block, ld/RP2040_FLASH_TIMECRIT_DOOM.ld).
//  2. The big runtime buffers below, carved at fixed offsets from the end of
//     those statics (__doom_shared_statics_end__): the frame buffer,
//     pd_render's working buffers, the vpatch lists — and the remainder is
//     the engine's zone memory (I_ZoneBase).
//
// Shared between the QMK-side game code (doom_mode.c, qmk_shim.c) and the
// POLYKYBD_QMK blocks inside the vendored engine (pd_render.cpp).
//
// MINIMUM POOL SIZE (if base/overlay.c overlays[] is ever shrunk to reclaim RAM):
// the fixed, non-shrinkable structure below is ~144 KB — statics ~20,824 + frame
// buffer 53,760 + pd_render 58,880 + vpatch 3,072 + compose 2,120 + mirror 1,664 +
// stack 4,096 — and the zone heap must stay >= upstream's ~58 KB working set. So
// the floor is ~205 KB (= 584 slots); the pool is 216,000 B / 600 slots today
// (NUM_OVERLAY_SLOTS), i.e. ~6 KB of zone slack left after the 630 -> 600 cut that
// reclaimed 10,800 B for the rest of the firmware. Shrinking further starves the
// zone heap. Shrinking the frame buffer / pd_render needs an engine-config
// change. The monolith link fails loudly on overflow; the DoomPack does not (it
// pins statics at the array address) — verify both flavours after any pool change.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SINGLE 320x168 8bpp view buffer (the 32 px status bar is vpatch-composed at
// scanout). Upstream double-buffers for the beam-racing scanout; our scanout
// is synchronous per frame and wipes are compiled out (NO_USE_WIPE), so the
// second buffer is dropped — pd_render's FRAME_BUFFER(i) macro maps both
// indices to this one (the pool cannot hold two frames + buffers + zone).
#define DOOM_ARENA_FB_BYTES     (320 * 168)
// pd_render working buffers, exact: list_buffer 47,296 + visplane_bit 6,720
// + patch decoder ring 3,584 + column heads 1,280 (all 4-aligned).
#define DOOM_ARENA_PD_BYTES     58880
// vpatchlists_t — upstream static_asserts sizeof < 0xc00.
#define DOOM_ARENA_VPATCH_BYTES 3072

// core1 game stack, carved from the very END of the pool (grows down from
// __doom_shared_end__). Upstream runs the game thread on a 2-4 K stack.
#define DOOM_ARENA_STACK_BYTES  4096

// Scanout compose scratch (core0 blit side): one 320 B 8bpp line the vpatch
// overlays (menus / HUD / status bar) are composed onto, plus one 360 B
// 1bpp OLED band buffer per viewport column (the blit is scanline-major so
// the vpatch data offsets advance strictly by row, like upstream's scanout).
#define DOOM_ARENA_COMPOSE_BYTES (320 + 5 * 360)

// Slave lockstep mirror mailbox (doom_mirror.h doom_mirror_t): the master TX
// ring + START outbox and the slave RX rolling window + START inbox. Sized
// with headroom over sizeof(doom_mirror_t) (static-asserted in doom_mode.c).
#define DOOM_ARENA_MIRROR_BYTES 1664

#define DOOM_ARENA_FB_OFF      0
#define DOOM_ARENA_PD_OFF      (DOOM_ARENA_FB_OFF + DOOM_ARENA_FB_BYTES)
#define DOOM_ARENA_VPATCH_OFF  (DOOM_ARENA_PD_OFF + DOOM_ARENA_PD_BYTES)
#define DOOM_ARENA_COMPOSE_OFF (DOOM_ARENA_VPATCH_OFF + DOOM_ARENA_VPATCH_BYTES)
#define DOOM_ARENA_MIRROR_OFF  (DOOM_ARENA_COMPOSE_OFF + DOOM_ARENA_COMPOSE_BYTES)
// Zone memory (Z_Init) takes everything from here to the game stack: with
// ~21 K of statics ahead of the arena that is ~84 K — comfortably above
// upstream's ~58 K (zone + wrapped-malloc heap) working set.
#define DOOM_ARENA_ZONE_OFF   (DOOM_ARENA_MIRROR_OFF + DOOM_ARENA_MIRROR_BYTES)

// ⚠️ Every arena offset must keep a 4-byte-aligned base 4-byte aligned: callers
// cast the result to structs holding uint32_t (doom_mirror_t), and an unaligned
// 32-bit access is a HardFault on the M0+ — the same failure that bricked the
// firmware applier (qmk#258). These asserts are what make that a build error
// instead of arithmetic nobody re-checks when a region is resized.
_Static_assert(DOOM_ARENA_FB_OFF      % 4u == 0u, "arena offset must stay 4-aligned");
_Static_assert(DOOM_ARENA_PD_OFF      % 4u == 0u, "arena offset must stay 4-aligned");
_Static_assert(DOOM_ARENA_VPATCH_OFF  % 4u == 0u, "arena offset must stay 4-aligned");
_Static_assert(DOOM_ARENA_COMPOSE_OFF % 4u == 0u, "arena offset must stay 4-aligned");
_Static_assert(DOOM_ARENA_MIRROR_OFF  % 4u == 0u, "arena offset must stay 4-aligned");
_Static_assert(DOOM_ARENA_ZONE_OFF    % 4u == 0u, "arena offset must stay 4-aligned");

// Arena base (= first byte after the engine statics) + offset, or NULL while
// game mode is inactive (doom_mode.c).
//
// ⚠️ Stays `uint8_t *` because this signature IS the pack ABI — it is handed to
// a loaded .plyx as `s_fw_api.arena_at` (doom_pack_abi.h). `void *` would be the
// better type for untyped arena storage and would make every
// `(doom_mirror_t *)doom_arena_at(...)` exempt from -Wcast-align, but changing a
// cross-boundary contract that a SIGNED pack is built against is not something
// to do on a warning's account. The alignment those casts rely on is asserted
// above instead, which is the substance; the one cast site outside the doom tree
// (split_sync.c) carries a narrow pragma pointing here.
uint8_t *doom_arena_at(unsigned offset);

#ifdef __cplusplus
}
#endif
