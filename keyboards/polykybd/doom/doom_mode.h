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

// Easter-egg arming state (field round 44 gate): typing IDDQD no longer
// starts the game — it ARMS the KC_IDDQD utilities-layer key, which then
// shows "IDDQD" (to_static_text) and starts the game when pressed. Master-
// local (the matcher runs there); stays armed until power-off, so the game
// can be relaunched from the menu without retyping.
bool doom_egg_armed(void);

// Position alias for the armed menu item (field round 46): the EEPROM
// dynamic keymap delivers KC_NO at the menu position on already-provisioned
// keyboards (the compiled keymaps[] KC_IDDQD only seeds a fresh EEPROM), so
// while armed + utilities layer active, KC_NO at [1,5] / [6,6] is rewritten
// to KC_IDDQD. Applied by both doom_process_record and update_displays.
uint16_t doom_egg_menu_keycode(uint16_t keycode, uint8_t row, uint8_t col);

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

// Fixed positional control layout (field round 23): the game controls are
// positions, independent of the active base/default layer — the cursor
// cluster on the slave's bottom outer four keys, use/enter beside it,
// fire/run/strafe/use/map pinned on the master; mirrored when the master is
// the right half. KC_NO = no fixed role (keymap keycode passes through).
// Input (doom_process_record) and the slave pad renderer both apply it.
uint16_t doom_ctl_keycode(uint8_t row, uint8_t col);

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

// Slave core0: true while the lockstep drone is engaged — reflected in the
// mirror-message ack byte (split_sync.c) so the master can re-offer a missed
// START and log engagement transitions.
bool doom_shim_mirror_engaged(void);

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

// Shared ESC exit-hint face ("hold" + "ESC" in the game's HUD font, legacy
// font pair without a WHX) — same contract; both halves' ESC corners use it
// so the layout is identical (doom_blit.c).
void doom_render_esc_key(void);

// Menu "Quit Game" pressed (I_Quit on the game core) — doom_tick exits the
// session like the ESC hold (qmk_shim.c).
bool doom_shim_quit_requested(void);

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

// Sound->RGB counters (qmk_shim.c): the engine's S_StartSound classifies
// every audible sound start — player weapon fire vs the rest of the world.
// core0's doom_rgb_task (doom_mode.c) edge-detects them into the synced
// poly_sync_t.doom_rgb byte.
extern volatile uint32_t doom_shim_snd_fire, doom_shim_snd_world;

// Sound->RGB render hook (doom_mode.c), called from rgb_matrix_indicators_kb:
// false = the doom cue owns this frame's LEDs, true = pass through to the
// normal indicators. Pass-through inline on variants without an RGB matrix.
#ifdef RGB_MATRIX_ENABLE
bool doom_rgb_indicators(void);
#else
static inline bool doom_rgb_indicators(void) { return true; }
#endif

#ifdef POLYKYBD_DOOM_PACK
// ── DoomPack dispatch (doom/PACK_DESIGN.md §3) ──────────────────────────────
// In the shipping-shape flavour the engine lives in the flashed PlyX pack;
// every doom_shim_* call above becomes a call through the pack's export
// table. doom_pack() NEVER returns NULL — before a pack is loaded (or after
// a refused one) it returns a static stub table whose functions are safe
// no-ops, so ungated call sites cannot fault. All in doom/doom_pack_load.c.
#include "doom_pack_abi.h"

const doom_pack_api_t *doom_pack(void);       // live table, or the stub
bool     doom_pack_load(uint8_t *pool, uint32_t pool_size); // validate+init the flashed pack
void     doom_pack_unload(void);              // back to the stub (session exit)
bool     doom_pack_loaded(void);
uint32_t doom_pack_arena_off(void);           // hdr.arena_off, 0 while unloaded

// The declarations above stay for signature documentation; the call sites
// (doom_mode.c / doom_blit.c / split_sync.c) expand to table calls.
#define doom_shim_take_frame()          (doom_pack()->take_frame())
#define doom_shim_release_frame()       (doom_pack()->release_frame())
#define doom_shim_drain_core1_log()     (doom_pack()->drain_core1_log())
#define doom_shim_gametic()             (doom_pack()->gametic())
#define doom_shim_video_type()          (doom_pack()->video_type())
#define doom_shim_compose_begin()       (doom_pack()->compose_begin())
#define doom_shim_compose_line(l, y)    (doom_pack()->compose_line((l), (y)))
#define doom_shim_hud_stats(h, a, m)    (doom_pack()->hud_stats((h), (a), (m)))
#define doom_shim_weapon_state(o, r)    (doom_pack()->weapon_state((o), (r)))
#define doom_shim_set_role(m)           (doom_pack()->set_role(m))
#define doom_shim_slave_view_live()     (doom_pack()->slave_view_live())
#define doom_shim_slave_wants_map_key() (doom_pack()->slave_wants_map_key())
#define doom_shim_mirror_engaged()      (doom_pack()->mirror_engaged())
#define doom_shim_attract_active()      (doom_pack()->attract_active())
#define doom_shim_quit_requested()      (doom_pack()->quit_requested())
#define doom_shim_drone_map_live()      (doom_pack()->drone_map_live())
#define doom_shim_tallnum_glyph(g, o, w, h) (doom_pack()->tallnum_glyph((g), (o), (w), (h)))
#define doom_shim_hufont_glyph(c, o, w, h)  (doom_pack()->hufont_glyph((c), (o), (w), (h)))
#define doom_shim_menu_snapshot(i, n, s)    (doom_pack()->menu_snapshot((i), (n), (s)))
#define doom_shim_menu_key_tile(r, c, t, l, a) (doom_pack()->menu_key_tile((r), (c), (t), (l), (a)))
#define doom_shim_face_index()          (doom_pack()->face_index())
#define doom_shim_face_oled(b, l)       (doom_pack()->face_oled((b), (l)))
#define doom_shim_progress              (*doom_pack()->progress)
#define doom_shim_snd_fire              (*doom_pack()->snd_fire)
#define doom_shim_snd_world             (*doom_pack()->snd_world)
#endif // POLYKYBD_DOOM_PACK

#else

static inline bool doom_mode_active(void) { return false; }
// Never armed without the game compiled in — the KC_IDDQD utilities-layer
// key then renders blank and stays inert, and the position alias passes
// every keycode through unchanged.
static inline bool doom_egg_armed(void) { return false; }
static inline uint16_t doom_egg_menu_keycode(uint16_t keycode, uint8_t row, uint8_t col) {
    (void)row; (void)col;
    return keycode;
}
static inline bool doom_process_record(uint16_t keycode, bool pressed, uint8_t row, uint8_t col) {
    (void)keycode; (void)pressed; (void)row; (void)col;
    return false;
}
static inline uint16_t doom_pad_keycode(uint8_t row, uint8_t col) {
    (void)row; (void)col;
    return 0; // KC_NO — no aliases without the game compiled in
}
static inline uint16_t doom_ctl_keycode(uint8_t row, uint8_t col) {
    (void)row; (void)col;
    return 0; // KC_NO
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
static inline bool doom_shim_mirror_engaged(void) { return false; }
static inline void doom_render_fire_key(void) {}
static inline void doom_render_use_key(void) {}
static inline void doom_render_esc_key(void) {}
static inline int  doom_status_face_render(uint8_t *buf1024) { (void)buf1024; return 0; }
static inline bool doom_status_scroll(void) { return true; }
static inline bool doom_rgb_indicators(void) { return true; }

#endif
