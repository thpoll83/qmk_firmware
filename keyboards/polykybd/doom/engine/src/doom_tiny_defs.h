// PolyKybd port: the rp2040-doom compile-definition set, as a header.
//
// Upstream passes these per-target on the CMake command line (src/CMakeLists.txt:
// small_doom_common + add_doom_tiny() + tiny_settings — the `doom_tiny` device
// build). In the QMK build we scope them to engine translation units instead by
// including this from the engine-local config.h, which every engine source pulls
// in early through doomtype.h — so QMK/ChibiOS code never sees them.
//
// Kept in upstream's grouping/order for diffability. Omitted on purpose:
//  * pico-sdk tuning (PICO_TIME_*, PICO_DISABLE_SHARED_IRQ_HANDLERS, stack
//    guards, SBRK, multicore lockout) — QMK/ChibiOS owns the runtime;
//  * scanvideo / render pin + I2C / I2S audio hardware config — no such
//    hardware; our video backend is the keycap blitter;
//  * EMU8950_* / USE_EMU8950_OPL / MUSX / midi-lump music stack — no sound
//    hardware on the keyboard (opl/ is not vendored); sound interfaces will be
//    stubbed at the s_sound/i_sound boundary instead;
//  * USE_PICO_NET / piconet — the split-UART lockstep transport replaces it;
//    plain NO_USE_NET stays;
//  * USB_SUPPORT / tinyusb — QMK owns USB.
#pragma once

// ---------------------------------------------------------------- platform --
// Marks every PolyKybd-port divergence inside engine sources — grep for it to
// see the full delta vs upstream.
#define POLYKYBD_QMK 1
// Upstream gets these from the pico-sdk CMake build; QMK's vendored SDK subset
// never defines them. We ARE a pico-sdk device target — without them the engine
// takes its host/SDL paths. panic() comes from QMK's pico_sdk_shims.c.
#define PICO_BUILD 1
#define PICO_ON_DEVICE 1
// PICO_RP2040 normally comes from pico/platform.h, which only TUs that include
// pico-sdk headers see (pd_render.cpp via hardware/interp.h). Every other
// engine TU left it undefined, so doomtype.h's shortptr_t compiled its
// RP2350 branch: SHORTPTR_BASE 0x20030000 (our zone at ~0x2002xxxx is BELOW
// that -> encode underflow) plus an unconditional `asm("bkpt #0")` range trap
// that halts core1 silently with no debugger attached — field test 2's
// progress=1 stall, first memblock_to_shortptr in Z_Init. Defining it here
// makes ALL engine TUs take the RP2040 branch (base 0x20000000, encode keeps
// addr bits [17:2] — covers all of main SRAM incl. the arena) and agree on ONE
// shortptr ABI. Identical to pico/platform.h's unguarded `#define PICO_RP2040
// 1`, so TUs seeing both are fine (same replacement list).
#define PICO_RP2040 1
// No binary_info metadata in the QMK image (bi_decl() collapses to a no-op —
// the header still needs to be on the include path).
#define PICO_NO_BINARY_INFO 1

#define PICO_HEAP_SIZE 0
#define USE_ZONE_FOR_MALLOC 1
#define NO_USE_MOUSE 1

// -------------------------------------------------- small_doom_common set --
#define SHRINK_MOBJ 1

#define DOOM_ONLY 1
#define DOOM_SMALL 1
#define DOOM_CONST 1

#define SOUND_LOW_PASS 1
#define NUM_SOUND_CHANNELS 8

#define NO_USE_CHECKSUM 1
#define NO_USE_RELOAD 1
#define USE_SINGLE_IWAD 1
#define NO_USE_WIPE 1
#define NO_USE_JOYSTICK 1
#define NO_USE_DEH 1
#define NO_USE_MUSIC_PACKS 1

#define USE_FLAT_MAX_256 1
#define USE_MEMMAP_ONLY 1

#define USE_LIGHTMAP_INDEXES 1

#define USE_ERASE_FRAME 1
#define NO_DRAW_MID 1
#define NO_DRAW_TOP 1
#define NO_DRAW_BOTTOM 1
#define NO_DRAW_MASKED 1
#define NO_DRAW_SKY 1
#define NO_DRAW_SPRITES 1
#define NO_DRAW_PSPRITES 1

#define NO_VISPLANE_GUTS 1
#define NO_VISPLANE_CACHES 1
#define NO_DRAWSEGS 1
#define NO_VISSPRITES 1

#define NO_MASKED_FLOOR_CLIP 1

#define PD_DRAW_COLUMNS 1
#define PD_DRAW_MARKERS 1
#define PD_DRAW_PLANES 1
#define PD_SCALE_SORT 1
#define PD_CLIP_WALLS 1
#define PD_QUANTIZE 1
#define PD_SANITY 1
#define PD_COLUMNS 1
#define PICO_DOOM 1
#define NO_USE_DS_COLORMAP 1
#define NO_USE_DC_COLORMAP 1
#define USE_READONLY_MMAP 1

#define NO_USE_TIMIDITY 1
#define NO_USE_GUS 1
#define NO_USE_LIBSAMPLERATE 1

#define NO_USE_STATE_MISC 1

#define USE_RAW_MAPNODE 1
#define USE_RAW_MAPVERTEX 1
#define USE_RAW_MAPSEG 1
#define USE_RAW_MAPLINEDEF 1
#define USE_RAW_MAPTHING 1

#define USE_INDEX_LINEBUFFER 1
#define NO_USE_ZLIGHT 1
#define NO_Z_ZONE_ID 1

#define Z_MALOOC_EXTRA_DATA 1
#define USE_THINKER_POOL 1
#define NO_INTERCEPTS_OVERRUN 1

#define TEMP_IMMUTABLE_DISABLED 1
#define USE_CONST_SFX 1
#define USE_CONST_MUSIC 1

#define NO_DEMO_RECORDING 1
#define PICO_NO_TIMING_DEMO 1
#define NO_USE_EXIT 1

// -------------------------------------------------------- doom_tiny target --
#define DOOM_TINY 1
#define NO_RDRAW 1

#define NO_USE_NET 1
// Keep upstream's piconet layer selected (the doom_tiny device build always
// has it; NO_USE_NET without it is an untested combination that doesn't even
// compile in d_loop.c). We do NOT compile upstream's pico-I2C piconet.c —
// piconet.h is the seam the split-UART lockstep tic transport (the study's
// USER_SYNC_DOOM_TIC) will implement instead.
#define USE_PICO_NET 1

#define SAVE_COMPRESSED 1
#define LOAD_COMPRESSED 1

#define NO_USE_ARGS 1
#define NO_USE_SAVE_CONFIG 1
#define NO_USE_FLOAT 1

#define USE_VANILLA_KEYBOARD_MAPPING_ONLY 1

#define NO_USE_LOADING_DISK 1

#define USE_WHD 1

#define NO_Z_MALLOC_USER_PTR 1

#define FIXED_SCREENWIDTH 1
#define FLOOR_CEILING_CLIP_8BIT 1

#define NO_SCREENSHOT 1
#define NO_USE_BOUND_CONFIG 1
#define USE_FPS 1

#define USE_MEMORY_WAD 1
#define NO_IERROR 1
#define NO_ZONE_DEBUG 1
#define NO_FILE_ACCESS 1
// Upstream device builds are MinSizeRel (NDEBUG); ours must be too — beyond
// perf, a newlib assert() firing on core1 would re-enter the ChibiOS console
// path that core1 must never touch (see the putchar_ relay in qmk_shim.c).
#ifndef NDEBUG
#define NDEBUG 1
#endif

// The WHX game-data image (doom1.whx is the WHD_SUPER_TINY format), mapped
// XIP at the PolyKybd resource-region slot (upstream doom_tiny: 0x10040000).
#define WHD_SUPER_TINY 1
#define DEMO1_ONLY 1
#define NO_USE_FINALE_CAST 1
#define NO_USE_FINALE_BUNNY 1
#define TINY_WAD_ADDR 0x10600000

// -------------------------------------------------- render (newhope / pd) --
#define PICODOOM_RENDER_NEWHOPE 1
#define MERGE_DISTSCALE0_INTO_VIEWCOSSINANGLE 1
