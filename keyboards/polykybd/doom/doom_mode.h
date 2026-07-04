// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PolyKybd "Can it run Doom?" easter egg — game-mode infrastructure.
// See ../DOOM_FEASIBILITY.md for the full study and ./README.md for the state
// of the port. Compiled only with POLYKYBD_DOOM (dev-harness builds:
// `qmk compile ... -e POLYKYBD_DOOM=yes`); the inline stubs below keep every
// call site free of #ifdefs and cost zero bytes in normal builds.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef POLYKYBD_DOOM

// True while game mode owns the keycap displays and the borrowed overlay pool.
bool doom_mode_active(void);

// Key-event hook, called first thing in process_record_user. Returns true when
// the event was consumed: in game mode EVERY key event is swallowed (the host
// must see no keystrokes while fragging); outside game mode it only feeds the
// trigger sequence matcher. row/col are the GLOBAL matrix coordinates — game
// mode aliases a few positions independent of their keymap keycode (the top
// outer corners act as ESC, the slave weapon-pad cells as the slot digits).
bool doom_process_record(uint16_t keycode, bool pressed, uint8_t row, uint8_t col);

// Position alias for game mode (also drives the slave-side pad rendering):
// KC_ESC for the top outer corner of either half, KC_1..KC_7 for the weapon
// pad cells (outer two columns of a half: inner col rows 0-3 = slots 1-4,
// outer col rows 1-3 = slots 5-7), KC_NO for everything else.
uint16_t doom_pad_keycode(uint8_t row, uint8_t col);

// Synced weapon state for the slave pad renderer (from poly_sync_t via the
// master): owned_mask bit N-1 = slot N owned, ready_slot = weapon in hand.
bool doom_weapon_state(uint8_t *owned_mask, uint8_t *ready_slot);

// Weapon-pad icon for a number-key slot (kdisp_draw_bitmap layout, from the
// shareware pickup sprites) or NULL — the pad then renders the digit.
const uint8_t *doom_weapon_icon(uint8_t slot, uint8_t *w, uint8_t *h);

// Frame/housekeeping tick, called from housekeeping_task_user. Runs the game
// only on the master (USB) half; a no-op on the slave and while inactive.
void doom_tick(void);

// True when `cmd` is a HID command that writes into the overlay pool / the
// fragment context — those are frozen while the pool is borrowed as game
// memory. All of them are ACKless bulk commands, so the caller must DROP them
// silently (replying would inject stale reports into the host's read stream).
bool doom_hid_frozen(uint8_t cmd);

// True for keys the slave-half control pad keeps lit while the game runs
// (poly_sync_t.doom_ctl + the update_displays filter): movement, fire/use,
// menu navigation, automap, weapon slots.
bool doom_key_is_control(uint16_t keycode);

// Arena layout inside the borrowed overlay pool (see DOOM_FEASIBILITY.md,
// "Game-mode RAM budget"). Valid only while game mode is active — the shim's
// I_ZoneBase hands the zone region to the engine's Z_Init.
uint8_t *doom_arena_framebuffer(void);   // 320x200 8bpp frame, DOOM_FB_SIZE bytes
uint8_t *doom_arena_zone(int *size);     // the rest of the pool (engine zone memory)

// Consumer side of the core0->core1 input ring: qmk_shim's I_StartTic drains
// key events into D_PostEvent on the game core. Returns false when empty.
bool doom_pop_key_event(uint8_t *key, bool *pressed);

// Frame handoff, implemented in qmk_shim.c over the renderer's pico_sync
// semaphores (which cannot be touched from QMK translation units — ChibiOS
// also defines semaphore_t): non-blocking take of a completed frame, and the
// release that unblocks the game for the next one.
bool doom_shim_take_frame(void);
void doom_shim_release_frame(void);

// core1 console relay (qmk_shim.c): drain the game core's buffered printf
// output through the real console on core0. Called from doom_tick.
void doom_shim_drain_core1_log(void);

// Engine boot progress breadcrumb (qmk_shim.c) for the no-frame heartbeat:
// 1 zone, 2 I_InitGraphics, 3 pd_init done, 4 first input pump.
extern volatile uint8_t doom_shim_progress;

// Core0-side peeks of core1 game state for the periodic stats line
// (qmk_shim.c — doom_mode.c cannot include engine headers).
int doom_shim_gametic(void);
unsigned doom_shim_video_type(void);

// vpatch overlay compose (qmk_shim.c) — menus/HUD/status bar on top of the
// view buffer, the keycap-blit equivalent of upstream's scanout compose.
// begin() once per consumed frame, then line() with a 320-byte 8bpp buffer
// for every canvas scanline y = 0..199 in ASCENDING order (fills the source
// row and draws the overlay patches for that line).
void doom_shim_compose_begin(void);
void doom_shim_compose_line(uint8_t *line, unsigned y);

// Player vitals for the outer-column keycap HUD (qmk_shim.c): health, armor
// and the ready weapon's ammo (-1 for fist/chainsaw). False outside a level.
bool doom_shim_hud_stats(int *health, int *armor, int *ammo);

// Weapon-slot state (qmk_shim.c): owned_mask bit N-1 = number-key slot N
// owned, ready_slot = slot of the weapon in hand. False outside a level.
bool doom_shim_weapon_state(uint8_t *owned_mask, uint8_t *ready_slot);

// Slave lockstep mirror (doom_mirror.h). Role is set by doom_mode.c before
// core1 launches; the view-live gate tells core0 whether the drone's frame
// (automap / intermission / finale) owns the slave viewport, and the map-key
// gate asks for a TAB injection (AM_Stop clears the automap on every level
// exit, so each new level re-toggles it).
void doom_shim_set_role(bool master);
bool doom_shim_slave_view_live(void);
bool doom_shim_slave_wants_map_key(void);

// True while the slave half's 5x5 viewport is owned by the mirror blitter —
// update_displays then leaves every non-pad key alone (doom_mode.c).
bool doom_slave_viewport_live(void);

// True while the blitter also owns the slave's bottom (thumb) key row — only
// during the ATTRACT mirror ("looks more uniform", field round 14); the map
// leaves that row to the pad so the cursor-key legends stay.
bool doom_slave_bottom_row_live(void);

// True while this half's engine is in the attract phase (title/demo loop, or
// the slave with no mirror engaged) — drives the status-OLED hardware scroll
// and the slave's full-viewport attract blit (qmk_shim.c).
bool doom_shim_attract_active(void);

// Status-OLED support (doom_mode.c over qmk_shim.c):
//  doom_status_face_render — the doomguy face on the 128x64 status OLED while
//  the master is in a level: 0 = no face (show the logo), 1 = face unchanged
//  (leave the panel alone), 2 = freshly rendered into buf1024 (write it).
//  doom_status_scroll — whether the logo should hardware-scroll (attract only).
int  doom_status_face_render(uint8_t *buf1024);
bool doom_status_scroll(void);

// Tall status-bar number glyphs decoded from the WHX (qmk_shim.c): glyph 0-9
// or DOOM_TALLNUM_MINUS into out8bpp (row-major w*h PLAYPAL indices; NULL =
// size probe only). False -> caller falls back to font digits.
#define DOOM_TALLNUM_MAX_W  16
#define DOOM_TALLNUM_MAX_H  24
#define DOOM_TALLNUM_MINUS  10
bool doom_shim_tallnum_glyph(uint8_t glyph, uint8_t *out8bpp, uint8_t *w, uint8_t *h);

// Small HUD-font glyph (STCFN, the menu/message font) decoded the same way:
// ch is the ASCII code 33..95 (uppercase-only — callers fold case). Used for
// the vitals word labels so the whole stat key is game artwork.
#define DOOM_HUFONT_MAX_W   12
#define DOOM_HUFONT_MAX_H   12
bool doom_shim_hufont_glyph(uint8_t ch, uint8_t *out8bpp, uint8_t *w, uint8_t *h);

// Fire/attack symbol (crosshair reticle) for the slave pad's Ctrl keys —
// drawn into the CURRENTLY selected keycap display's buffer; the caller owns
// display selection, buffer clear and send (doom_blit.c).
void doom_render_fire_key(void);

// Use/open symbol (a door leaf with a knob) for the pad's Space key — same
// currently-selected-display contract as doom_render_fire_key (doom_blit.c).
void doom_render_use_key(void);

// Readable menu mirror (qmk_shim.c, field round 17). Master core0 samples
// the engine's active menu (item vpatch handles + selection; 0 = nothing
// mirrorable up); the slave's core0 renders per-keycap tiles from its OWN
// WHX's vpatches — column 0 the blinking skull on the selected row, columns
// 1+ the item at 2x. doom_shim_drone_map_live gates the map's viewport frame.
int  doom_shim_menu_snapshot(uint16_t *items, int max_items, int *item_on);
bool doom_shim_menu_key_tile(uint8_t vr, uint8_t vc, uint8_t *tile360,
                             const uint8_t *luma256, bool skull_alt);
bool doom_shim_drone_map_live(void);

// Doomguy face for the status OLED (qmk_shim.c): current face index (-1 when
// not in a level) and the 2x-scaled render into a 128x64 page buffer.
int  doom_shim_face_index(void);
bool doom_shim_face_oled(uint8_t *oled_buf, const uint8_t *luma256);

#else

static inline bool doom_mode_active(void) { return false; }
static inline bool doom_process_record(uint16_t keycode, bool pressed, uint8_t row, uint8_t col) {
    (void)keycode; (void)pressed; (void)row; (void)col;
    return false;
}
static inline uint16_t doom_pad_keycode(uint8_t row, uint8_t col) {
    (void)row; (void)col;
    return 0; // KC_NO — no aliases without the game compiled in
}
static inline bool doom_weapon_state(uint8_t *owned_mask, uint8_t *ready_slot) {
    (void)owned_mask; (void)ready_slot;
    return false;
}
static inline const uint8_t *doom_weapon_icon(uint8_t slot, uint8_t *w, uint8_t *h) {
    (void)slot; (void)w; (void)h;
    return NULL;
}
static inline void doom_tick(void) {}
static inline bool doom_hid_frozen(uint8_t cmd) { (void)cmd; return false; }
// Never queried when doom is compiled out (doom_ctl is never set), but the
// update_displays filter references it unconditionally.
static inline bool doom_key_is_control(uint16_t keycode) { (void)keycode; return true; }
static inline bool doom_slave_viewport_live(void) { return false; }
static inline bool doom_slave_bottom_row_live(void) { return false; }
static inline void doom_render_fire_key(void) {}
static inline void doom_render_use_key(void) {}
static inline int  doom_status_face_render(uint8_t *buf1024) { (void)buf1024; return 0; }
static inline bool doom_status_scroll(void) { return true; }

#endif
