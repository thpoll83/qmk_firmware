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

void I_StartTic(void) {
    // Drain the key events doom_process_record collected on core0 (SPSC ring
    // in doom_mode.c) into the engine — this runs on the game core.
    doom_shim_progress = 4;
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
// piconet: single-player stub — never connects, never receives tics. The real
// implementation rides USER_SYNC_DOOM_TIC over the split UART (both halves
// simulate in input-lockstep; see DOOM_FEASIBILITY.md).
// ---------------------------------------------------------------------------

boolean net_client_connected = false;
char    player_name[MAXPLAYERNAME];

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

void piconet_new_local_tic(int tic) { (void)tic; }
int piconet_maybe_recv_tic(int fromtic) { (void)fromtic; return -1; }

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
