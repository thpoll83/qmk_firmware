// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// QMK platform shim for the rp2040-doom engine — replaces upstream's
// src/pico/{i_system,i_timer,i_picosound}.c backends (which sit on pico-sdk
// stdio/audio/scanvideo) with implementations over the PolyKybd firmware.
//
// Deliberately NOT here yet:
//  * the pd_* renderer interface (pd_render.cpp port — the video backend that
//    will feed doom_blit);
//  * the piconet_* lockstep transport (implemented over split-UART
//    USER_SYNC_DOOM_TIC transactions per DOOM_FEASIBILITY.md) — a single-player
//    stub lives below until then;
//  * input pumping into D_PostEvent (wired from doom_process_record).
#include "doomtype.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_sound.h"
#include "i_video.h"
#include "v_video.h"
#include "d_event.h"
#include "v_patch.h"          // vpatch_* accessors, vpatchlist_t
#include "whddata.h"          // VPATCH_* handle enum (VPATCH_M_THERMM)
#include "doom/r_data.h"      // resolve_vpatch_handle
#include "w_wad.h"
#include "m_misc.h"
#include "deh_str.h"
#include "f_wipe.h"
#include "piconet.h"
#include "picoflash.h"

#include "doomdef.h"          // SCREENWIDTH
#include "doom/doomstat.h"    // players/gamestate/consoleplayer (HUD stats)
#include "doom/d_items.h"     // weaponinfo (ready-weapon ammo type)
#include "picodoom.h"         // pd_init()
#include "z_zone.h"           // zone allocator (core1 malloc redirection)

#include "doom_mode.h"
#include "doom_arena.h"

#include "hardware/timer.h"   // time_us_64 (QMK's vendored pico-sdk, IRQ-free read)
#include "pico/platform.h"    // panic()
#include "pico/sem.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef POLYKYBD_DOOM

// ---------------------------------------------------------------------------
// core1-safe console relay. core1 must NEVER enter QMK's console path:
// sendchar -> usb_endpoint_in_send does osalSysLock() + a blocking
// obqWriteTimeout() that suspends the calling ChibiOS *thread* — core1 has no
// ChibiOS context, so the first write that needs to block wedges the core
// forever (field 2026-07-03: the engine froze inside its 4th boot printf,
// after the banner/Z_Init lines slipped through the non-blocking fast path).
// All printf output is routed here via `-Wl,--wrap=putchar_` (the lib/printf
// -> sendchar funnel in quantum/logging/print.c): core0 passes through,
// core1 pushes into a lock-free SPSC ring that doom_tick drains on core0.
// ---------------------------------------------------------------------------

extern void __real_putchar_(char c);

#define C1LOG_LEN 1024u
static volatile uint8_t  c1log[C1LOG_LEN];
static volatile uint16_t c1log_w, c1log_r;

void __wrap_putchar_(char c) {
    if (get_core_num() == 0) {
        __real_putchar_(c);
        return;
    }
    uint16_t w = c1log_w;
    if ((uint16_t)(w - c1log_r) >= C1LOG_LEN) {
        return; // ring full — drop rather than ever block core1
    }
    c1log[w % C1LOG_LEN] = (uint8_t)c;
    __asm volatile("dmb" ::: "memory");
    c1log_w = (uint16_t)(w + 1);
}

void doom_shim_drain_core1_log(void) {
    uint16_t r = c1log_r;
    // bounded per pass so a chatty engine can't stall housekeeping
    for (int n = 0; n < 256 && r != c1log_w; ++n) {
        __real_putchar_((char)c1log[r % C1LOG_LEN]);
        ++r;
    }
    c1log_r = r;
}

// Boot progress breadcrumb, readable from core0 (doom_tick's no-frame
// heartbeat): 1 = zone handed over, 2 = I_InitGraphics entered, 3 = pd_init
// done, 4 = first tic input pump ran.
volatile uint8_t doom_shim_progress;

// I_Error lands here (i_system.h, POLYKYBD_QMK branch of the NO_IERROR
// macro) instead of upstream's bare __breakpoint(): a bkpt with no debugger
// halts the core with zero trace — the shortptr range trap failed exactly
// this way in the field (2026-07-03, "progress=1 heartbeats, no output").
// lib/printf's vprintf funnels through the wrapped putchar_, so from core1
// the message lands in the relay ring and doom_tick surfaces it on the
// console; then park the core (ESC-hold exit resets core1 regardless).
void doom_shim_error(const char *fmt, ...) {
    printf("doom: I_Error: ");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    for (;;) {
        __asm volatile("wfe");
    }
}

// ---------------------------------------------------------------------------
// core1 allocator redirection (LDFLAGS --wrap=malloc/calloc/free/realloc/
// strdup). Upstream's device build wraps malloc/calloc/free into the zone
// (USE_ZONE_FOR_MALLOC + pico_wrap_function); without it any engine
// allocation that survives the tiny-build #ifdefs would go to newlib malloc,
// whose lock/sbrk path is as core1-hostile as the console. Defense-in-depth:
// nm shows no live malloc/strdup call site in the current image (the
// suspected M_SetConfigDir->strdup path compiles out under
// NO_USE_BOUND_CONFIG — the 2026-07-03 progress=1 stall was actually the
// shortptr bkpt trap, see doom_tiny_defs.h PICO_RP2040), but one config flip
// away it comes back, and the wrap keeps it on the zone where upstream has it.
// Core-aware rather than mode-aware: the engine runs exclusively on core1,
// QMK exclusively on core0, so core0 always gets the real newlib heap.
// Z_MallocNoUser never returns NULL (it errors out), so no NULL handling.
// ---------------------------------------------------------------------------

extern void *__real_malloc(size_t size);
extern void *__real_calloc(size_t count, size_t size);
extern void  __real_free(void *mem);
extern void *__real_realloc(void *mem, size_t size);
extern char *__real_strdup(const char *s);

void *__wrap_malloc(size_t size) {
    if (get_core_num() == 0) {
        return __real_malloc(size);
    }
    return Z_MallocNoUser((int)size, PU_STATIC);
}

void *__wrap_calloc(size_t count, size_t size) {
    if (get_core_num() == 0) {
        return __real_calloc(count, size);
    }
    void *p = Z_MallocNoUser((int)(count * size), PU_STATIC);
    memset(p, 0, count * size);
    return p;
}

void __wrap_free(void *mem) {
    if (get_core_num() == 0) {
        __real_free(mem);
        return;
    }
    if (mem != NULL) {
        Z_Free(mem);
    }
}

void *__wrap_realloc(void *mem, size_t size) {
    if (get_core_num() == 0) {
        return __real_realloc(mem, size);
    }
    if (mem == NULL) {
        return Z_MallocNoUser((int)size, PU_STATIC);
    }
    // No Z_Realloc, and upstream's device build doesn't wrap realloc either —
    // no engine device path should get here. Fail loud (relayed via the ring).
    panic("doom: realloc on core1");
}

char *__wrap_strdup(const char *s) {
    if (get_core_num() == 0) {
        return __real_strdup(s);
    }
    size_t n = strlen(s) + 1;
    char  *p = Z_MallocNoUser((int)n, PU_STATIC);
    memcpy(p, s, n);
    return p;
}

// Frame handoff between the renderer and the core0 blit loop — upstream
// defines these in its scanvideo i_video.c, which we replace. Zero-initialised
// (0 permits) until I_InitGraphics runs on core1, so the core0-side probe
// below is safe from boot.
semaphore_t render_frame_ready, display_frame_freed;

// The display side owns the screen-melt pacing: under DOOM_TINY the game
// loop is `do { D_Display(); } while (wipestate)` (d_main.c D_RunFrame) with
// TryRunTics OUTSIDE the loop, and pd_render's wipestate machine only exits
// once `wipe_min` reaches 200 — which upstream's scanvideo i_video.c advances
// once per DISPLAYED frame. Replace the scanout and nobody melts the columns:
// gametic freezes at the first title->demo transition while the renderer
// loops wipe frames forever (field round 4, 2026-07-03: frames climbing,
// gametic parked at 172, vt=5). This is upstream's per-frame column advance
// (i_video.c new_frame init, verbatim incl. the every-other-frame `regular`
// toggle — upstream uses display_overlay_index, which flips per frame).
// Runs on core0 only; the renderer writes wipe_yoffsets_raw strictly before
// releasing the first WIPE frame and afterwards only polls wipe_min.
static void advance_wipe_columns(void) {
    if (!wipe_yoffsets_raw || !wipe_yoffsets) {
        return;
    }
    static bool regular;
    regular = !regular;
    int new_wipe_min = 200;
    for (int i = 0; i < SCREENWIDTH; i++) {
        int v;
        if (wipe_yoffsets_raw[i] < 0) {
            if (regular) {
                wipe_yoffsets_raw[i]++;
            }
            v = 0;
        } else {
            int dy = (wipe_yoffsets_raw[i] < 16) ? (1 + wipe_yoffsets_raw[i] + regular) / 2 : 4;
            if (wipe_yoffsets_raw[i] + dy > 200) {
                v = 200;
            } else {
                wipe_yoffsets_raw[i] += dy;
                v = wipe_yoffsets_raw[i];
            }
        }
        wipe_yoffsets[i] = v;
        if (v < new_wipe_min) {
            new_wipe_min = v;
        }
    }
    wipe_min = (uint8_t)new_wipe_min;
}

// Latched per consumed frame (the renderer flips next_* while we display).
static uint8_t shim_disp_video_type;
static uint8_t shim_disp_overlay_index;

bool doom_shim_take_frame(void) {
    if (!sem_available(&render_frame_ready)) {
        return false;
    }
    sem_acquire_blocking(&render_frame_ready);
    shim_disp_video_type    = next_video_type;
    shim_disp_overlay_index = next_overlay_index;
    if (shim_disp_video_type == VIDEO_TYPE_WIPE && wipe_min <= 200) {
        advance_wipe_columns();
    }
    return true;
}

void doom_shim_release_frame(void) {
    sem_release(&display_frame_freed);
}

// ---------------------------------------------------------------------------
// vpatch overlay compose — menus, HUD text, the status bar. Upstream draws
// these at scanout, per scanline, AFTER palette conversion (pico/i_video.c
// draw_vpatch + the vpatch_starters/next/doff bookkeeping in
// new_frame_init_overlays_palette_and_wipe). Our scanout is the keycap blit,
// so the same machinery runs here at 8bpp: pixels stay PLAYPAL indices (the
// blit's luma dither converts), the per-patch 4-bit palettes resolve to index
// bytes instead of RGB565. The blit calls doom_shim_compose_begin() once per
// consumed frame, then doom_shim_compose_line() for every canvas scanline in
// ASCENDING order — the running data offsets (vpatch_doff) are sequential,
// exactly like upstream's beam-ordered scanout.
// ---------------------------------------------------------------------------

static bool    compose_overlays_active;
static uint8_t shared_pal8[NUM_SHARED_PALETTES][16];

// True while this half is a lockstep drone showing the automap (defined with
// the mirror block below) — the compose blacks out the status-bar band then.
static bool doom_mirror_drone_map_active(void);

extern uint8_t *frame_buffer_0;   // defined below (video-backend globals)

// 8bpp port of upstream draw_vpatch (pico/i_video.c): identical decode, every
// `palette[pal[v]]` (RGB565) becomes `pal[v]` (a PLAYPAL index). The stbar
// XIP-DMA cache special-case is dropped — the generic vp4_solid path decodes
// the same bytes. Returns the data offset after this row (the doff advance).
static unsigned draw_vpatch8(uint8_t *dest, const patch_t *patch, const vpatchlist_t *vp, unsigned off) {
    int repeat = vp->entry.repeat;
    dest += vp->entry.x;
    int w = vpatch_width(patch);
    const uint8_t *data0 = vpatch_data(patch);
    const uint8_t *data = data0 + off;
    if (!vpatch_has_shared_palette(patch)) {
        const uint8_t *pal = vpatch_palette(patch);
        switch (vpatch_type(patch)) {
            case vp4_runs: {
                uint8_t *p = dest;
                uint8_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 1; i < len; i += 2) {
                        unsigned v = *data++;
                        *p++ = pal[v & 0xf];
                        *p++ = pal[v >> 4];
                    }
                    if (len & 1) {
                        *p++ = pal[(*data++) & 0xf];
                    }
                    if (p == pend) break;
                }
                break;
            }
            case vp4_alpha: {
                uint8_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    unsigned v = *data++;
                    if (v & 0xf) p[0] = pal[v & 0xf];
                    if (v >> 4) p[1] = pal[v >> 4];
                    p += 2;
                }
                if (w & 1) {
                    unsigned v = *data++;
                    if (v & 0xf) p[0] = pal[v & 0xf];
                }
                break;
            }
            case vp4_solid: {
                uint8_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    unsigned v = *data++;
                    p[0] = pal[v & 0xf];
                    p[1] = pal[v >> 4];
                    p += 2;
                }
                if (w & 1) {
                    unsigned v = *data++;
                    p[0] = pal[v & 0xf];
                }
                break;
            }
            case vp6_runs: {
                uint8_t *p = dest;
                uint8_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 3; i < len; i += 4) {
                        unsigned v = *data++;
                        v |= (*data++) << 8;
                        v |= (*data++) << 16;
                        *p++ = pal[v & 0x3f];
                        *p++ = pal[(v >> 6) & 0x3f];
                        *p++ = pal[(v >> 12) & 0x3f];
                        *p++ = pal[(v >> 18) & 0x3f];
                    }
                    len &= 3;
                    if (len--) {
                        unsigned v = *data++;
                        *p++ = pal[v & 0x3f];
                        if (len--) {
                            v >>= 6;
                            v |= (*data++) << 2;
                            *p++ = pal[v & 0x3f];
                            if (len--) {
                                v >>= 6;
                                v |= (*data++) << 4;
                                *p++ = pal[v & 0x3f];
                            }
                        }
                    }
                    if (p == pend) break;
                }
                break;
            }
            case vp8_runs: {
                uint8_t *p = dest;
                uint8_t *pend = dest + w;
                uint8_t gap;
                while (0xff != (gap = *data++)) {
                    p += gap;
                    int len = *data++;
                    for (int i = 0; i < len; i++) {
                        *p++ = pal[*data++];
                    }
                    if (p == pend) break;
                }
                break;
            }
            case vp_border: {
                dest[0] = *data++;
                uint8_t col = *data++;
                for (int i = 1; i < w - 1; i++) dest[i] = col;
                dest[w - 1] = *data++;
                break;
            }
            default:
                break;
        }
    } else {
        unsigned sp = vpatch_shared_palette(patch);
        const uint8_t *pal = shared_pal8[sp];
        switch (vpatch_type(patch)) {
            case vp4_solid: {
                uint8_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    unsigned v = *data++;
                    p[0] = pal[v & 0xf];
                    p[1] = pal[v >> 4];
                    p += 2;
                }
                if (w & 1) {
                    unsigned v = *data++;
                    dest[w - 1] = pal[v & 0xf];
                }
                break;
            }
            case vp4_alpha: {
                uint8_t *p = dest;
                for (int i = 0; i < w / 2; i++) {
                    unsigned v = *data++;
                    if (v & 0xf) p[0] = pal[v & 0xf];
                    if (v >> 4) p[1] = pal[v >> 4];
                    p += 2;
                }
                if (w & 1) {
                    unsigned v = *data++;
                    if (v & 0xf) p[0] = pal[v & 0xf];
                }
                break;
            }
            default:
                break;
        }
    }
    if (repeat) {
        int rw = w;
        if (vp->entry.patch_handle == VPATCH_HANDLE(VPATCH_M_THERMM)) rw--; // upstream "hackity hack"
        for (int i = 0; i < repeat * rw; i++) {
            dest[rw + i] = dest[i];
        }
    }
    return (unsigned)(data - data0);
}

void doom_shim_compose_begin(void) {
    compose_overlays_active =
        vpatchlists && shim_disp_video_type >= FIRST_VIDEO_TYPE_WITH_OVERLAYS;
    if (!compose_overlays_active) {
        return;
    }
    // Per-frame init, ported from new_frame_init_overlays_palette_and_wipe:
    // bucket the overlay entries by start row (starters), clear the active
    // linked list (index 0 is the sentinel/head) and the running offsets.
    memset(vpatchlists->vpatch_next, 0, sizeof(vpatchlists->vpatch_next));
    memset(vpatchlists->vpatch_starters, 0, sizeof(vpatchlists->vpatch_starters));
    memset(vpatchlists->vpatch_doff, 0, sizeof(vpatchlists->vpatch_doff));
    vpatchlist_t *overlays = vpatchlists->overlays[shim_disp_overlay_index];
    for (int i = overlays->header.size - 1; i > 0; i--) {
        vpatchlists->vpatch_next[i] = vpatchlists->vpatch_starters[overlays[i].entry.y];
        vpatchlists->vpatch_starters[overlays[i].entry.y] = (uint8_t)i;
    }
    // Shared palettes as PLAYPAL indices (upstream builds RGB565 here).
    for (int i = 0; i < NUM_SHARED_PALETTES; i++) {
        const patch_t *patch = resolve_vpatch_handle(vpatch_for_shared_palette[i]);
        const uint8_t *pal = vpatch_palette(patch);
        for (int j = 0; j < 16; j++) {
            shared_pal8[i][j] = pal[j];
        }
    }
}

void doom_shim_compose_line(uint8_t *line, unsigned y) {
    // Source select, mirroring the scanline_func_* source math: rows above
    // MAIN_VIEWHEIGHT live in "the other" frame buffer at (y - 32) — with the
    // single shared view buffer that alias holds exactly the full-screen
    // page's bottom rows for SINGLE, and don't matter for DOUBLE (the status
    // bar overdraws them, so feed black underneath).
    if (y >= MAIN_VIEWHEIGHT && shim_disp_video_type == VIDEO_TYPE_DOUBLE) {
        memset(line, 0, SCREENWIDTH);
        // Drone automap: leave the band BLACK — the automap only covers the
        // 168 view rows (f_h = SCREENHEIGHT - ST_HEIGHT), and composing the
        // 1:1 tiny status bar under it read as "the bottom key row does not
        // clear" (field round 11). The master keeps its status bar.
        if (doom_mirror_drone_map_active()) {
            return;
        }
    } else {
        unsigned sy = y < MAIN_VIEWHEIGHT ? y : y - 32;
        memcpy(line, frame_buffer_0 + sy * SCREENWIDTH, SCREENWIDTH);
    }
    if (!compose_overlays_active || y >= 200) {
        return;
    }
    // Activate entries starting on this row (sorted insert into the active
    // list), then draw every active patch and retire finished ones — a
    // verbatim port of the scanout loop in pico/i_video.c.
    int prev = 0;
    for (int vp = vpatchlists->vpatch_starters[y]; vp;) {
        int next = vpatchlists->vpatch_next[vp];
        while (vpatchlists->vpatch_next[prev] && vpatchlists->vpatch_next[prev] < vp) {
            prev = vpatchlists->vpatch_next[prev];
        }
        vpatchlists->vpatch_next[vp] = vpatchlists->vpatch_next[prev];
        vpatchlists->vpatch_next[prev] = (uint8_t)vp;
        prev = vp;
        vp = next;
    }
    vpatchlist_t *overlays = vpatchlists->overlays[shim_disp_overlay_index];
    prev = 0;
    for (int vp = vpatchlists->vpatch_next[prev]; vp; vp = vpatchlists->vpatch_next[prev]) {
        const patch_t *patch = resolve_vpatch_handle(overlays[vp].entry.patch_handle);
        int yoff = (int)y - overlays[vp].entry.y;
        if (yoff < vpatch_height(patch)) {
            vpatchlists->vpatch_doff[vp] =
                (uint16_t)draw_vpatch8(line, patch, &overlays[vp], vpatchlists->vpatch_doff[vp]);
            prev = vp;
        } else {
            vpatchlists->vpatch_next[prev] = vpatchlists->vpatch_next[vp];
        }
    }
}

// ---------------------------------------------------------------------------
// status-bar artwork decode: the doomguy face for the status OLED and the
// tall red numbers for the vitals keys, straight from the WHX vpatches. A
// zeroed vpatchlist_t entry (x 0, no repeat) turns draw_vpatch8 into a plain
// row-major decoder; the compose above stays untouched.
// ---------------------------------------------------------------------------

extern int ST_FaceIndex(void);   // st_stuff.c POLYKYBD_QMK getter

// Same 4x4 Bayer thresholds as the keycap blit (doom_blit.c BAYER4) — the
// face dither matches the game view's look.
static const uint8_t shim_bayer4[4][4] = {
    {  8, 136,  40, 168},
    {200,  72, 232, 104},
    { 56, 184,  24, 152},
    {248, 120, 216,  88},
};

#define SHIM_FACE_MAX_W 48u

// Live face index while a level runs on this half's engine, else -1. The
// index changes with health/damage/rampage — the caller redraws only on a
// change (a plain word read of core1 state).
int doom_shim_face_index(void) {
    if (gamestate != GS_LEVEL || !doom_shim_progress) {
        return -1;
    }
    return ST_FaceIndex();
}

// Render the current doomguy face, scaled 2x + centered, into a 128x64
// SSD1306 page buffer (1024 B, byte=(y/8)*128+x). False when no face is
// available (not in a level / patch unresolvable) — the caller shows the logo.
bool doom_shim_face_oled(uint8_t *oled_buf, const uint8_t *luma256) {
    int idx = doom_shim_face_index();
    if (idx < 0) {
        return false;
    }
    const patch_t *p = resolve_vpatch_handle((vpatch_handle_large_t)(VPATCH_NAME(STFST00) + idx));
    if (!p) {
        return false;
    }
    const unsigned w = vpatch_width(p);
    const unsigned h = vpatch_height(p);
    if (!w || !h || w > SHIM_FACE_MAX_W || 2 * w > 128 || 2 * h > 64) {
        return false;
    }
    memset(oled_buf, 0, 128u * 64u / 8u);
    uint8_t       row[SHIM_FACE_MAX_W];
    vpatchlist_t  vp;
    memset(&vp, 0, sizeof(vp));

    // Pass 1 — per-face auto-levels (fontconvert's -N lesson: the sprite
    // palette is brownish-dark, luma mostly 40..150, so an ungained dither
    // reads dim — field round 15 "could be brighter / more contrast"). Take
    // the ~90th-percentile luma of the drawn pixels as the white point (a
    // plain max would let one bright highlight cap the gain).
    unsigned off = 0;
    uint16_t hist[16] = {0};
    uint32_t counted  = 0;
    for (unsigned sy = 0; sy < h; sy++) {
        memset(row, 0, w);
        off = draw_vpatch8(row, p, &vp, off);
        for (unsigned sx = 0; sx < w; sx++) {
            const uint8_t v = luma256[row[sx]];
            if (v > 8) {   // skip background/outline
                hist[v >> 4]++;
                counted++;
            }
        }
    }
    unsigned ref = 255;
    if (counted) {
        uint32_t cum = 0;
        for (int b = 15; b >= 0; b--) {
            cum += hist[b];
            if (cum * 10 >= counted) {   // top 10 % reached
                ref = (unsigned)(b * 16 + 15);
                break;
            }
        }
    }
    const unsigned gain = (ref > 32) ? (255u * 256u) / ref : 256u;   // 8.8 fixed

    // Pass 2 — decode again (cheap) and dither the gained luma at 2x.
    memset(&vp, 0, sizeof(vp));
    off = 0;
    const unsigned x0 = (128u - 2 * w) / 2;
    const unsigned y0 = (64u - 2 * h) / 2;
    for (unsigned sy = 0; sy < h; sy++) {
        memset(row, 0, w);
        off = draw_vpatch8(row, p, &vp, off);
        for (unsigned sx = 0; sx < w; sx++) {
            unsigned v = ((unsigned)luma256[row[sx]] * gain) >> 8;
            if (v > 255) {
                v = 255;
            }
            for (unsigned dy = 0; dy < 2; dy++) {
                const unsigned Y = y0 + 2 * sy + dy;
                for (unsigned dx = 0; dx < 2; dx++) {
                    const unsigned X = x0 + 2 * sx + dx;
                    if (v > shim_bayer4[Y & 3][X & 3]) {
                        oled_buf[(Y >> 3) * 128u + X] |= (uint8_t)(1u << (Y & 7));
                    }
                }
            }
        }
    }
    return true;
}

// Decode one tall status-bar number glyph (0-9, DOOM_TALLNUM_MINUS) into
// out8bpp (row-major w*h PLAYPAL indices; pass NULL to only fetch the size —
// callers measure the whole value before deciding to draw). False when the
// engine/patch is unavailable — the caller falls back to font digits.
bool doom_shim_tallnum_glyph(uint8_t glyph, uint8_t *out8bpp, uint8_t *w, uint8_t *h) {
    if (!doom_shim_progress) {
        return false;
    }
    vpatch_handle_large_t handle;
    if (glyph <= 9) {
        handle = (vpatch_handle_large_t)(VPATCH_NAME(STTNUM0) + glyph);
    } else if (glyph == DOOM_TALLNUM_MINUS) {
        handle = VPATCH_NAME(STTMINUS);
    } else {
        return false;
    }
    const patch_t *p = resolve_vpatch_handle(handle);
    if (!p) {
        return false;
    }
    const unsigned pw = vpatch_width(p);
    const unsigned ph = vpatch_height(p);
    if (!pw || !ph || pw > DOOM_TALLNUM_MAX_W || ph > DOOM_TALLNUM_MAX_H) {
        return false;
    }
    *w = (uint8_t)pw;
    *h = (uint8_t)ph;
    if (out8bpp) {
        memset(out8bpp, 0, (size_t)pw * ph);
        vpatchlist_t vp;
        memset(&vp, 0, sizeof(vp));
        unsigned off = 0;
        for (unsigned y = 0; y < ph; y++) {
            off = draw_vpatch8(out8bpp + (size_t)y * pw, p, &vp, off);
        }
    }
    return true;
}

// Decode one small HUD-font glyph (STCFN — the game's menu/message font)
// into out8bpp, same contract as the tall numbers above. `ch` is the ASCII
// code; the font covers HU_FONTSTART 33 ('!') .. 95 ('_'), uppercase-only —
// the caller folds case. Contiguity of the STCFN vpatch handles mirrors the
// STFST face run (whddata.h lists them in ASCII order).
bool doom_shim_hufont_glyph(uint8_t ch, uint8_t *out8bpp, uint8_t *w, uint8_t *h) {
    if (!doom_shim_progress || ch < 33 || ch > 95) {
        return false;
    }
    const patch_t *p = resolve_vpatch_handle((vpatch_handle_large_t)(VPATCH_NAME(STCFN033) + (ch - 33)));
    if (!p) {
        return false;
    }
    const unsigned pw = vpatch_width(p);
    const unsigned ph = vpatch_height(p);
    if (!pw || !ph || pw > DOOM_HUFONT_MAX_W || ph > DOOM_HUFONT_MAX_H) {
        return false;
    }
    *w = (uint8_t)pw;
    *h = (uint8_t)ph;
    if (out8bpp) {
        memset(out8bpp, 0, (size_t)pw * ph);
        vpatchlist_t vp;
        memset(&vp, 0, sizeof(vp));
        unsigned off = 0;
        for (unsigned y = 0; y < ph; y++) {
            off = draw_vpatch8(out8bpp + (size_t)y * pw, p, &vp, off);
        }
    }
    return true;
}

// (bitcount8_table comes from p_maputl.c)

// ---------------------------------------------------------------------------
// system: zone memory + process control
// ---------------------------------------------------------------------------

byte *I_ZoneBase(int *size) {
    byte *zone = (byte *)doom_arena_zone(size);
    printf("doom: zone memory %p, %x bytes (borrowed overlay pool)\n", zone, *size);
    doom_shim_progress = 1;
    return zone;
}

void I_Quit(void) {
    // Menu quit on a keyboard means "leave the easter egg" — that path exits
    // game mode from doom_tick. Reaching the engine's own quit is a bug until
    // that is wired, so fail loudly rather than return from a NORETURN.
    panic("doom: I_Quit");
}

void I_Tactile(int on, int off, int total) {
    (void)on; (void)off; (void)total;
}

boolean I_GetMemoryValue(unsigned int offset, void *value, int size) {
    (void)offset; (void)value; (void)size;
    return false;   // no default.cfg memory hack values
}

void I_PrintBanner(const char *msg) {
    printf("%s\n", msg);
}

void I_PrintStartupBanner(const char *gamedescription) {
    printf("doom: %s\n", gamedescription);
}

void I_BindVariables(void) {}
void I_BindInputVariables(void) {}
void I_BindSoundVariables(void) {}
void I_BindVideoVariables(void) {}

void panic_unsupported(void) {
    panic("doom: unsupported");
}

// ---------------------------------------------------------------------------
// timer: 35 Hz tics over the free-running 1 MHz hardware timer
// ---------------------------------------------------------------------------

void I_InitTimer(void) {}

int I_GetTimeMS(void) {
    return (int)(time_us_64() / 1000);
}

int I_GetTime(void) {
    return (int)((time_us_64() * TICRATE) / 1000000);
}

void I_Sleep(int ms) {
    // Only reached from error/wait paths; the engine's own pacing is tic-based.
    uint64_t until = time_us_64() + (uint64_t)ms * 1000;
    while (time_us_64() < until) {}
}

// ---------------------------------------------------------------------------
// video backend globals + stubs (the pd_render/doom_blit integration will
// give these real behaviour; the engine's non-render paths only need them
// to exist and be sane)
// ---------------------------------------------------------------------------

// (wipestate / pre_wipe_state are defined by pd_render.cpp)
boolean          screenvisible  = true;
isb_int8_t       usegamma       = 0;
pixel_t         *I_VideoBuffer  = NULL;

// The engine's view buffer — a SINGLE arena-backed 320x168 frame (upstream
// double-buffers for the beam-racing scanout; see pd_render's FRAME_BUFFER
// macro for why one suffices here).
uint8_t *frame_buffer_0;

// Video-backend state shared with pd_render (upstream: pico/i_video.c). The
// scanout loop will consume these; until then they just need to exist.
volatile uint8_t interp_in_use;
uint8_t         *wipe_yoffsets;      // position of start of y in each column
int16_t         *wipe_yoffsets_raw;
uint32_t        *wipe_linelookup;    // offset of each line from start of screenbuffer
uint8_t          next_video_type;
uint8_t          next_frame_index;
uint8_t          next_overlay_index;
volatile uint8_t wipe_min;
// Music restart handshake (upstream: i_oplmusic.c — the emu8950 stack is not
// compiled; pd_render pokes this at level transitions).
uint8_t restart_song_state;

// Palette index selected by the engine (0 = normal; >0 = damage/pickup/rad
// flashes) — the keycap blitter folds it into the dither luma.
int doom_shim_palette = 0;

// Core0-side peeks for doom_tick's periodic stats line (plain word reads of
// core1-written state — the exact values don't matter, the *movement* does:
// a frozen gametic vs a frozen frame count point at different subsystems).
int doom_shim_gametic(void) {
    extern int gametic;   // d_loop.c
    return gametic;
}

// Player vitals for the outer-column keycap HUD (core0 render): plain reads
// of core1-written ints — a torn read shows a stale number for one frame.
// Returns false outside a level (demo screens, menus over the title).
bool doom_shim_hud_stats(int *health, int *armor, int *ammo) {
    if (gamestate != GS_LEVEL || !doom_shim_progress) {
        return false;
    }
    const player_t *p = &players[consoleplayer];
    *health = p->health;
    *armor  = p->armorpoints;
    ammotype_t at = weaponinfo[p->readyweapon].ammo;
    *ammo = (at == am_noammo) ? -1 : p->ammo[at];
    return true;
}

// Weapon-slot state for the slave-half weapon pad, as DOOM's number-key
// slots: owned_mask bit N-1 = slot N owned, ready_slot = the slot of the
// weapon in hand. Slot 1 covers fist+chainsaw, slot 3 shotgun(+super).
bool doom_shim_weapon_state(uint8_t *owned_mask, uint8_t *ready_slot) {
    if (gamestate != GS_LEVEL || !doom_shim_progress) {
        return false;
    }
    // Indexed by weapontype_t; NUMWEAPONS is an enum constant (9 incl.
    // wp_supershotgun even in the DOOM_ONLY build), so list all explicitly.
    static const uint8_t weapon_slot[] = {
        1, // wp_fist
        2, // wp_pistol
        3, // wp_shotgun
        4, // wp_chaingun
        5, // wp_missile
        6, // wp_plasma
        7, // wp_bfg
        1, // wp_chainsaw
        3, // wp_supershotgun
    };
    _Static_assert(sizeof(weapon_slot) == NUMWEAPONS, "weapon_slot covers weapontype_t");
    const player_t *p = &players[consoleplayer];
    uint8_t mask = 0;
    for (int w = 0; w < NUMWEAPONS; w++) {
        if (p->weaponowned[w]) {
            mask |= (uint8_t)(1u << (weapon_slot[w] - 1));
        }
    }
    *owned_mask = mask;
    *ready_slot = (p->readyweapon < NUMWEAPONS) ? weapon_slot[p->readyweapon] : 0;
    return true;
}

unsigned doom_shim_video_type(void) {
    return next_video_type;
}

void I_InitGraphics(void) {
    // Mirrors the essentials of upstream I_InitGraphics (pico/i_video.c):
    // frame handoff semaphores + pd_init(). Deliberately NOT here yet: the
    // core1 launch running pd_core1_loop (the renderer will deadlock on its
    // core1 semaphores if the game is started before that lands) and the
    // scanout loop feeding doom_blit.
    doom_shim_progress = 2;
    frame_buffer_0 = doom_arena_at(DOOM_ARENA_FB_OFF);
    I_VideoBuffer  = frame_buffer_0;
    sem_init(&render_frame_ready, 0, 2);
    sem_init(&display_frame_freed, 1, 2);
    pd_init();
    doom_shim_progress = 3;
    screenvisible = true;
}

void I_GraphicsCheckCommandLine(void) {}
void I_CheckIsScreensaver(void) {}
void I_SetWindowTitle(const char *title) { (void)title; }
void I_StartFrame(void) {}

static void doom_mirror_poll_start(void); // slave START apply (mirror block below)

void I_StartTic(void) {
    // Drain the key events doom_process_record collected on core0 (SPSC ring
    // in doom_mode.c) into the engine — this runs on the game core.
    doom_shim_progress = 4;
    doom_mirror_poll_start();
    uint8_t key;
    bool    pressed;
    while (doom_pop_key_event(&key, &pressed)) {
        event_t ev = {0};
        ev.type  = pressed ? ev_keydown : ev_keyup;
        ev.data1 = key;
        D_PostEvent(&ev);
    }
}

void I_UpdateNoBlit(void) {}

void I_SetPaletteNum(int num) {
    doom_shim_palette = num;
}

void I_Endoom(should_be_const byte *data) {
    (void)data;   // no exit text screen on a keyboard
}

void I_StartTextInput(void) {}
void I_StopTextInput(void) {}

// ---------------------------------------------------------------------------
// sound + music: silent backend (the keyboard has no speaker; the RGB matrix
// "sound" substitute hooks in at S_StartSound level later, not down here)
// ---------------------------------------------------------------------------

isb_int8_t snd_pitchshift = 0;

void I_InitSound(boolean use_sfx_prefix) { (void)use_sfx_prefix; }
void I_ShutdownSound(void) {}

int I_GetSfxLumpNum(should_be_const sfxinfo_t *sfxinfo) {
    // Real lookup (mirrors upstream i_picosound): S_* caches lump numbers even
    // when nothing ever plays them.
    char namebuf[9];
    if (sfxinfo->link != NULL) {
        sfxinfo = sfxinfo->link;
    }
    M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfxinfo->name));
    return W_GetNumForName(namebuf);
}

void I_UpdateSound(void) {}
void I_UpdateSoundParams(int channel, int vol, int sep) { (void)channel; (void)vol; (void)sep; }

int I_StartSound(should_be_const sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch) {
    (void)sfxinfo; (void)vol; (void)sep; (void)pitch;
    return channel;
}

void I_StopSound(int channel) { (void)channel; }
boolean I_SoundIsPlaying(int channel) { (void)channel; return false; }
void I_PrecacheSounds(should_be_const sfxinfo_t *sounds, int num_sounds) { (void)sounds; (void)num_sounds; }

// pd_render drives the music fade-out on level end through these (upstream
// i_picosound.c); silent backend = nothing fades.
void I_PicoSoundFade(bool in) { (void)in; }
bool I_PicoSoundFading(void) { return false; }

void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) { (void)volume; }
void I_PauseSong(void) {}
void I_ResumeSong(void) {}
void *I_RegisterSong(should_be_const void *data, int len) { (void)data; (void)len; return NULL; }
void I_UnRegisterSong(void *handle) { (void)handle; }
void I_PlaySong(void *handle, boolean looping) { (void)handle; (void)looping; }
void I_StopSong(void) {}
boolean I_MusicIsPlaying(void) { return false; }
void I_SetOPLDriverVer(opl_driver_ver_t ver) { (void)ver; }

// ---------------------------------------------------------------------------
// piconet: the slave lockstep mirror (doom_mirror.h). Upstream's I2C piconet
// is a 2-player lobby; here the seam carries a ONE-WAY mirror instead — the
// master half stays a plain single-player game (net_client_connected false,
// so d_loop's netgame gating never touches its timing) whose ticcmds stream
// to the slave, and the slave becomes a chocolate-doom drone at the master's
// G_DoNewGame (D_StartDoomMirror). The lobby entry points stay stubs — the
// menus never offer multiplayer on a keyboard.
// ---------------------------------------------------------------------------

#include "d_loop.h"           // ticdata/BACKUPTICS/gametic, D_StartDoomMirror
#include "net_client.h"       // drone (the mirror's spectator mode)
#include "doom/g_game.h"      // G_DeferedInitNew
#include "doom/d_main.h"      // D_StartTitle (BREAK -> back to the attract)
#include "doom/am_map.h"      // AM_Stop
#include "doom_mirror.h"

extern void G_DoNewGame(boolean net);    // g_game.c (not in g_game.h)
extern void AM_SetCheating(int level);   // am_map.c POLYKYBD_QMK reveal hook
extern void AM_SetFatLines(int fat);     // am_map.c POLYKYBD_QMK 2x2 dots + big arrow

_Static_assert(sizeof(ticcmd_t) == DOOM_MIRROR_CMD_BYTES, "mirror carries raw ticcmds");

boolean net_client_connected = false;
char    player_name[MAXPLAYERNAME];

// Which side of the mirror this engine instance is. Set by doom_mode.c before
// core1 launches (and the per-session state reset with it), so there is no
// cross-core ordering to worry about.
static bool     s_mirror_master;
static bool     s_mirror_started;    // slave: a START has been applied
static bool     s_mirror_game_sent;  // master: a START went out (gates BREAK)
static uint32_t s_start_applied;     // slave: seq of the last applied START

extern boolean menuactive; // m_menu.c (no header decl in DOOM_TINY)

void doom_shim_set_role(bool master) {
    s_mirror_master    = master;
    s_mirror_started   = false;
    s_mirror_game_sent = false;
    s_start_applied    = 0;
    // Re-entry reset (field round 17: "no viewport on the slave — always on
    // the second or third start, the first run is good"): the engine's
    // .data is NOT re-initialised on a core1 relaunch, and a first session
    // that engaged the drone leaves `drone`/`net_client_connected` true —
    // the slave's next boot then waits for net tics forever instead of
    // running its attract (no frames -> no viewport). A normal engine boot
    // never resets these (they belong to net negotiation), so do it here,
    // on core0, before core1 launches. menuactive likewise (an ESC-hold
    // exit tears the session down with the menu open).
    drone                = false;
    net_client_connected = false;
    menuactive           = false;
}

static doom_mirror_t *doom_mirror_box(void) {
    _Static_assert(sizeof(doom_mirror_t) <= DOOM_ARENA_MIRROR_BYTES, "mirror fits its arena carve");
    return (doom_mirror_t *)doom_arena_at(DOOM_ARENA_MIRROR_OFF);
}

void piconet_init(void) {}
void piconet_start_host(int8_t deathmatch, int8_t epi, int8_t skill) { (void)deathmatch; (void)epi; (void)skill; }
void piconet_start_client(void) {}
void piconet_stop(void) {}
bool piconet_client_check_for_dropped_connection(void) { return false; }
void piconet_start_game(void) {}

int piconet_get_lobby_state(lobby_state_t *state) {
    if (state) {
        state->status = lobby_no_connection;
        state->nplayers = 0;
    }
    return 0;
}

// Master, game core, once per locally built tic (d_loop.c BuildNewTic calls
// this unconditionally in the PolyKybd build): publish the cmd to the TX
// ring. maketic counts from 0 each session and only ever advances by one, so
// the ring is indexed by absolute tic; any mismatch means the ring overflowed
// (core0 / the bridge stalled for ~2 s) and the mirror is dead for the
// session — cheaper and safer than resync machinery for a failure that only
// a dead split link produces.
void piconet_new_local_tic(int tic) {
    if (!s_mirror_master) {
        return;
    }
    doom_mirror_t *m = doom_mirror_box();
    if (!m || m->tx_overflow) {
        return;
    }
    if ((uint32_t)tic != m->tx_w || m->tx_w - m->tx_r >= DOOM_MIRROR_TX_LEN) {
        m->tx_overflow = 1;
        return;
    }
    memcpy(m->tx_cmds[m->tx_w % DOOM_MIRROR_TX_LEN],
           &ticdata[tic % BACKUPTICS].cmds[consoleplayer], DOOM_MIRROR_CMD_BYTES);
    __asm volatile("dmb" ::: "memory");
    m->tx_w = (uint32_t)tic + 1;
}

// Slave, game core, from GetLowTic while connected: move cmds from the
// rolling window into ticdata and advance recvtic. Bounded by the ticdata
// ring (BACKUPTICS=5): only slots for tics < gametic + BACKUPTICS are free —
// the rest stays queued in the window. If the producer's roll head has moved
// past a tic we still need, the slave stalled beyond the window — freeze
// (the drone shows its frozen frame; the next START re-arms).
int piconet_maybe_recv_tic(int fromtic) {
    if (s_mirror_master) {
        return fromtic;
    }
    doom_mirror_t *m = doom_mirror_box();
    if (!m) {
        return fromtic;
    }
    if (m->break_in || m->rx_broken) {
        net_client_connected = false;
        return fromtic;
    }
    uint32_t t = (uint32_t)fromtic;
    while (t < m->rx_w && t < (uint32_t)gametic + BACKUPTICS) {
        if (t < m->rx_r) {
            m->rx_broken = 1;
            net_client_connected = false;
            break;
        }
        ticcmd_set_t *set = &ticdata[t % BACKUPTICS];
        memset(set, 0, sizeof(*set));   // players 1-3 not ingame
        memcpy(&set->cmds[0], m->rx_cmds[t % DOOM_MIRROR_RX_LEN], DOOM_MIRROR_CMD_BYTES);
        __asm volatile("dmb" ::: "memory");
        if (t < m->rx_r) {              // producer rolled over the slot mid-copy
            m->rx_broken = 1;
            net_client_connected = false;
            break;
        }
        t++;
    }
    return (int)t;
}

// Master, game core, from the G_DoNewGame hook: publish START(settings, tic)
// on the latest-wins control channel. gametic is the tic G_Ticker is
// currently running — the first tic the fresh level consumes, and its cmd is
// already in the slave's rolling window (built, and mirrored, before it ran).
void doom_shim_mirror_new_game(int skill, int episode, int map) {
    if (!s_mirror_master) {
        return;
    }
    doom_mirror_t *m = doom_mirror_box();
    if (!m || m->tx_overflow) {
        return;
    }
    m->ctl_out_kind  = DOOM_MIRROR_MSG_START;
    m->ctl_out_tic   = (uint32_t)gametic;
    m->ctl_out_skill = (uint8_t)skill;
    m->ctl_out_epi   = (uint8_t)episode;
    m->ctl_out_map   = (uint8_t)map;
    __asm volatile("dmb" ::: "memory");
    m->ctl_out_seq++;
    s_mirror_game_sent = true;
}

// Master, game core, from the G_DoPlayDemo / G_DoLoadGame hooks: the sim is
// about to leave the mirrorable path (demo tics come from the lump; a loaded
// save restores state the slave doesn't have). Only meaningful while a
// mirrored game is engaged — the attract loop restarts its demo every cycle
// and must NOT keep breaking the slave's own attract view. Slave role: no-op
// (its attract plays through the same G_DoPlayDemo).
void doom_shim_mirror_break(void) {
    if (!s_mirror_master || !s_mirror_game_sent) {
        return;
    }
    doom_mirror_t *m = doom_mirror_box();
    if (!m || m->tx_overflow) {
        return;
    }
    s_mirror_game_sent = false;
    m->ctl_out_kind = DOOM_MIRROR_MSG_BREAK;
    __asm volatile("dmb" ::: "memory");
    m->ctl_out_seq++;
}

// Slave, game core, polled from I_StartTic (which runs every built tic, drone
// or not): apply a freshly received START — replay the master's G_DoNewGame
// with the mirrored settings and engage drone lockstep at the same gametic —
// and unwind a BREAK / dead mirror back to this half's OWN attract loop (the
// viewport then shows the title/demo again instead of a frozen frame).
static void doom_mirror_poll_start(void) {
    if (s_mirror_master) {
        return;
    }
    doom_mirror_t *m = doom_mirror_box();
    if (!m) {
        return;
    }
    if ((m->break_in || m->rx_broken) && s_mirror_started) {
        printf("doom: mirror BREAK — back to the attract\n");
        s_mirror_started = false;
        drone = false;
        net_client_connected = false;
        if (automapactive) {
            AM_Stop();
        }
        m->break_in  = 0;
        m->rx_broken = 0;
        D_StartTitle();
        return;
    }
    if (m->break_in) {
        m->break_in = 0; // not engaged — nothing to unwind
    }
    uint32_t seq = m->start_in_seq;
    if (seq == s_start_applied) {
        return;
    }
    __asm volatile("dmb" ::: "memory");
    uint32_t tic = m->start_in_tic;
    s_start_applied = seq;
    if (m->rx_r > tic) {
        // The window no longer holds the start tic's cmd — lockstep from that
        // tic is impossible. Stay down until the next START.
        m->rx_broken = 1;
        return;
    }
    printf("doom: mirror START tic=%u e%um%u skill %u\n",
           (unsigned)tic, m->start_in_epi, m->start_in_map, m->start_in_skill);
    AM_SetCheating(2);   // drone never runs the 3D renderer -> IDDT reveal
    AM_SetFatLines(1);   // 2x2 dots + doubled arrow for the keycap dither
    G_DeferedInitNew((skill_t)m->start_in_skill, m->start_in_epi, m->start_in_map, false);
    G_DoNewGame(false);
    D_StartDoomMirror((int)tic);
    m->rx_broken = 0;
    m->break_in  = 0;
    s_mirror_started = true;
}

// Core0-side gates for the slave's blit/pad split (racy plain reads of
// core1 state — worth at most one frame of lag).
bool doom_shim_slave_view_live(void) {
    if (s_mirror_master) {
        return false;
    }
    doom_mirror_t *m = doom_mirror_box();
    if (!m || m->break_in || m->rx_broken) {
        return false;
    }
    if (!s_mirror_started) {
        // Attract mirror: the slave's OWN title/demo fills the viewport until
        // a real game starts (field round 11 — "a good idea until game
        // start"; both halves' attracts run near-parallel, unsynced).
        return true;
    }
    if (!net_client_connected) {
        return false;
    }
    if (gamestate == GS_LEVEL) {
        // Only the automap — never the drone's first-person view (it flashed
        // for the few tics before the injected TAB landed, field round 11).
        return automapactive;
    }
    return gamestate == GS_INTERMISSION || gamestate == GS_FINALE;
}

// True while the live mirror sits in a level without its automap up — the
// core0 side answers by injecting the map-toggle key (AM_Stop() runs on
// every level exit, so each new level needs a fresh toggle). NOT view_live
// gated: while the map is down the in-level view is deliberately not blitted,
// which is exactly when the toggle is needed.
bool doom_shim_slave_wants_map_key(void) {
    if (s_mirror_master || !s_mirror_started || !net_client_connected) {
        return false;
    }
    return gamestate == GS_LEVEL && !automapactive;
}

static bool doom_mirror_drone_map_active(void) {
    return !s_mirror_master && s_mirror_started && automapactive;
}

// True while this half's engine is in its attract phase: the master on the
// title/demo loop (menus over it included — gamestate stays GS_DEMOSCREEN),
// the slave whenever no mirror is engaged (its own attract runs then). Drives
// the status-OLED scroll and the slave's full-viewport attract blit.
bool doom_shim_attract_active(void) {
    if (!s_mirror_master) {
        return !s_mirror_started;
    }
    return gamestate == GS_DEMOSCREEN;
}

// Core0 wrapper of the drone-map gate for the blit's map frame (round 17).
bool doom_shim_drone_map_live(void) {
    return doom_mirror_drone_map_active();
}

// ---------------------------------------------------------------------------
// Readable menu mirror (field round 17: the 1:1 tiny on-canvas menu was
// unreadable — "I most of the time blindly select the start"). The master
// samples its engine's active menu (M_MenuSnapshot), doom_mode.c ships it
// over a MENU mirror message, and the slave redraws the items in the game's
// big menu font at 2x — one item per key row, the blinking skull in the
// first column — on otherwise dark keycaps. M_Drawer suppresses the tiny
// master-side draw while this path is engaged (doom_shim_menu_mirrored).
// ---------------------------------------------------------------------------

extern int M_MenuSnapshot(uint16_t *items, int max_items, int *item_on);

// Game core (m_menu.c M_Drawer): suppress the on-canvas item draw whenever
// the mirror exists — the pump ships every snapshot change to the slave.
int doom_shim_menu_mirrored(void) {
    return s_mirror_master && doom_mirror_box() != NULL;
}

// Master core0: sample the engine's menu for the mirror pump (plain reads
// of core1 state — a torn read self-corrects on the next sample).
int doom_shim_menu_snapshot(uint16_t *items, int max_items, int *item_on) {
    if (!s_mirror_master || !doom_shim_progress) {
        return 0;
    }
    return M_MenuSnapshot(items, max_items, item_on);
}

// Menu items can be wide ("READ THIS!" ~121 px); anything wider is skipped.
#define SHIM_MENU_PATCH_MAX_W 160u

// The 4 visible menu rows window: keep the selection on-screen, pinned to
// the last row once the menu is deeper than the window.
static uint8_t shim_menu_win_start(uint8_t count, uint8_t item_on) {
    if (count <= 4 || item_on <= 3) {
        return 0;
    }
    uint8_t start = (uint8_t)(item_on - 3);
    if (start > (uint8_t)(count - 4)) {
        start = (uint8_t)(count - 4);
    }
    return start;
}

// Decode `p` row-sequentially and stamp it into the 72x40 keycap tile of
// viewport column `vc` (page-major 5x72 B), scaled `scale`x, at canvas x0 /
// tile-local y0. Text is thresholded solid; shaded artwork (the skull) is
// Bayer-dithered with canvas-continuous coordinates.
static void shim_menu_stamp(uint8_t *tile360, const patch_t *p, int x0, int y0,
                            unsigned scale, uint8_t vc, const uint8_t *luma256,
                            bool dither, unsigned canvas_row0) {
    const unsigned w = vpatch_width(p);
    const unsigned h = vpatch_height(p);
    const int      win0 = (int)vc * 72 - 20; // canvas x of this tile's x=0
    uint8_t        row[SHIM_MENU_PATCH_MAX_W];
    vpatchlist_t   vp;
    memset(&vp, 0, sizeof(vp));
    unsigned off = 0;
    for (unsigned sy = 0; sy < h; sy++) {
        memset(row, 0, w);
        off = draw_vpatch8(row, p, &vp, off);
        for (unsigned d = 0; d < scale; d++) {
            const int Y = y0 + (int)(sy * scale + d);
            if (Y < 0 || Y >= 40) {
                continue;
            }
            for (unsigned sx = 0; sx < w; sx++) {
                const uint8_t v = luma256[row[sx]];
                for (unsigned e = 0; e < scale; e++) {
                    const int cx = x0 + (int)(sx * scale + e);
                    const int tx = cx - win0;
                    if (tx < 0 || tx >= 72) {
                        continue;
                    }
                    const bool on = dither ? (v > shim_bayer4[(canvas_row0 + (unsigned)Y) & 3][cx & 3])
                                           : (v >= 36);
                    if (on) {
                        tile360[(size_t)(Y >> 3) * 72 + tx] |= (uint8_t)(1u << (Y & 7));
                    }
                }
            }
        }
    }
}

// Render the menu view for keycap (view row 0-3, view col 0-4) into tile360
// (zeroed here). Column 0 carries the skull on the selected row; columns 1+
// carry the item at 2x from canvas x=52 (the windows don't overlap, so each
// key decodes at most one patch). False when no menu is up / not decodable —
// the caller drops back to the regular attract/map view.
bool doom_shim_menu_key_tile(uint8_t vr, uint8_t vc, uint8_t *tile360,
                             const uint8_t *luma256, bool skull_alt) {
    doom_mirror_t *m = doom_mirror_box();
    if (s_mirror_master || !m || !doom_shim_progress || vr > 3 || vc > 4) {
        return false;
    }
    const uint8_t count = m->menu_in_count;
    if (!count) {
        return false;
    }
    const uint8_t item_on = m->menu_in_item_on;
    const uint8_t idx     = (uint8_t)(shim_menu_win_start(count, item_on) + vr);
    memset(tile360, 0, 5u * 72u);
    if (vc == 0) {
        if (idx == item_on) {
            const patch_t *p = resolve_vpatch_handle(
                skull_alt ? VPATCH_NAME(M_SKULL2) : VPATCH_NAME(M_SKULL1));
            if (p && vpatch_width(p) <= 44 && vpatch_height(p) <= 40) {
                shim_menu_stamp(tile360, p, 8, (int)(40 - vpatch_height(p)) / 2,
                                1, 0, luma256, true, (unsigned)vr * 40u);
            }
        }
        return true;
    }
    if (idx >= count) {
        return true; // past the menu: stays dark
    }
    const uint16_t handle = m->menu_in_items[idx];
    if (!handle) {
        return true; // unnamed slot: stays dark
    }
    const patch_t *p = resolve_vpatch_handle((vpatch_handle_large_t)handle);
    if (!p) {
        return true;
    }
    const unsigned w = vpatch_width(p);
    const unsigned h = vpatch_height(p);
    if (!w || !h || w > SHIM_MENU_PATCH_MAX_W || 2 * h > 40) {
        return true;
    }
    shim_menu_stamp(tile360, p, 52, (int)(40 - 2 * h) / 2, 2, vc, luma256,
                    false, (unsigned)vr * 40u);
    return true;
}

// ---------------------------------------------------------------------------
// picoflash: savegame sector writes — disabled in v1 (savegames skipped per
// the study; a real implementation must route through the firmware's flash
// helpers with the same IRQ/XIP discipline as fw_staging).
// ---------------------------------------------------------------------------

void picoflash_sector_program(uint32_t flash_offs, const uint8_t *data) {
    (void)flash_offs; (void)data;
    panic("doom: savegame flash writes not supported");
}

#endif // POLYKYBD_DOOM
