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

#include "doom_mode.h"

#include "hardware/timer.h"   // time_us_64 (QMK's vendored pico-sdk, IRQ-free read)
#include "pico/platform.h"    // panic()

#include <stdio.h>

#ifdef POLYKYBD_DOOM

// ---------------------------------------------------------------------------
// system: zone memory + process control
// ---------------------------------------------------------------------------

byte *I_ZoneBase(int *size) {
    byte *zone = (byte *)doom_arena_zone(size);
    printf("doom: zone memory %p, %x bytes (borrowed overlay pool)\n", zone, *size);
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

boolean          screenvisible  = true;
isb_int8_t       usegamma       = 0;
pixel_t         *I_VideoBuffer  = NULL;
wipestate_t      wipestate      = WIPESTATE_NONE;
pre_wipe_state_t pre_wipe_state = PRE_WIPE_NONE;

// Palette index selected by the engine (0 = normal; >0 = damage/pickup/rad
// flashes) — the keycap blitter folds it into the dither luma.
int doom_shim_palette = 0;

void I_InitGraphics(void) {
    I_VideoBuffer = (pixel_t *)doom_arena_framebuffer();
    screenvisible = true;
}

void I_GraphicsCheckCommandLine(void) {}
void I_CheckIsScreensaver(void) {}
void I_SetWindowTitle(const char *title) { (void)title; }
void I_StartFrame(void) {}

void I_StartTic(void) {
    // TODO(engine): drain the key events doom_process_record collected into
    // D_PostEvent() here.
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
