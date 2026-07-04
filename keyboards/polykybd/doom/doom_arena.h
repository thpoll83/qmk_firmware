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

// Arena base (= first byte after the engine statics) + offset, or NULL while
// game mode is inactive (doom_mode.c).
uint8_t *doom_arena_at(unsigned offset);

#ifdef __cplusplus
}
#endif
