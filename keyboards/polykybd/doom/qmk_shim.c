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
#include "w_wad.h"
#include "m_misc.h"
#include "deh_str.h"
#include "f_wipe.h"
#include "piconet.h"
#include "picoflash.h"

#include "doomdef.h"          // SCREENWIDTH
#include "picodoom.h"         // pd_init()

#include "doom_mode.h"
#include "doom_arena.h"

#include "hardware/timer.h"   // time_us_64 (QMK's vendored pico-sdk, IRQ-free read)
#include "pico/platform.h"    // panic()
#include "pico/sem.h"

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

// Frame handoff between the renderer and the core0 blit loop — upstream
// defines these in its scanvideo i_video.c, which we replace. Zero-initialised
// (0 permits) until I_InitGraphics runs on core1, so the core0-side probe
// below is safe from boot.
semaphore_t render_frame_ready, display_frame_freed;

bool doom_shim_take_frame(void) {
    if (!sem_available(&render_frame_ready)) {
        return false;
    }
    sem_acquire_blocking(&render_frame_ready);
    return true;
}

void doom_shim_release_frame(void) {
    sem_release(&display_frame_freed);
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
