// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define RP2040

//#define EXTERNAL_FLASH_SIZE (8 * 1024 * 1024)

//#define UNICODE_SELECTED_MODES UNICODE_MODE_LINUX, UNICODE_MODE_MACOS, UNICODE_MODE_WINCOMPOSE
#define UNICODE_CYCLE_PERSIST true
#define UNICODE_KEY_WINC      KC_RIGHT_ALT

#define TAP_CODE_DELAY 4

#define DIODE_DIRECTION ROW2COL


// Split keyboard
// https://docs.qmk.fm/#/feature_split_keyboard?id=split-keyboard
// Full-duplex two-wire link (GP5 TX, GP4 RX). Replaces the previous single-wire
// half-duplex: removes the bus-turnaround/line-float hazard and drives push-pull
// both ways (no pull-up needed) — fewer wire-noise glitches on the split UART.
// The physical cable is wired STRAIGHT (GP5<->GP5, GP4<->GP4), so we rely on
// SERIAL_USART_PIN_SWAP, which swaps TX/RX only on the MASTER half's init path
// (serial_vendor.c: master_init swaps, slave_init does not). One identical image
// on both halves therefore produces the logical crossover at runtime by role —
// no per-side build, no EEPROM handedness needed.
#define SERIAL_USART_TX_PIN GP5
#define SERIAL_USART_RX_PIN GP4
#define SERIAL_USART_FULL_DUPLEX
#define SERIAL_USART_PIN_SWAP
// Split-link baud is set per variant via SELECT_SOFT_SERIAL_SPEED in halconf.h
// (0 = 460800). See platforms/chibios/drivers/serial_usart.h for the mapping.

#define SPLIT_TRANSPORT_MIRROR
// #define SPLIT_LAYER_STATE_ENABLE
// #define SPLIT_LED_STATE_ENABLE
// #define SPLIT_MODS_ENABLE
#define SPLIT_WPM_ENABLE
// NOTE: the QMK transaction table is near-full (NUM_TOTAL_TRANSACTIONS <= 32, the
// hard cap). This list dropped the dead USER_SYNC_FW_UP_QUERY, folded the four
// flash-staging transactions (BEGIN/CHUNK/COMMIT/STATUS) into one
// USER_SYNC_FLASH_STAGE (op word selects the sub-operation), and merged the apply
// + reboot transactions into one USER_SYNC_RESET (action byte selects apply vs
// reboot) — freeing 5 slots total. A new sync can still MULTIPLEX onto an existing
// id by a distinct payload size, like the MRU snapshots and the doom mirror
// messages both do on USER_SYNC_OVERLAY_MAP_DATA (user_sync_overlay_map_data_handler).
// One freed slot is the GENERIC slave->master pull channel USER_SYNC_SLAVE_DATA.
// Like USER_SYNC_FLASH_STAGE it is op-dispatched: the master's request carries a
// `kind` byte (enum slave_data_kind) selecting which slave-side payload to return,
// so future slave->master data (more sensors, telemetry, diagnostics) reuses this
// ONE slot instead of spending another of the scarce 32. Its first user is the
// LTR-559: when DRIVEing brightness/idle the master pulls {avg lux, proximity}
// from the sensor (right) half via transaction_rpc_exec, so it works in either
// USB orientation. Only compiled in when the sensor can drive (needs the slot).
#ifdef POLYKYBD_LTR559_DRIVE
#    define POLY_LTR559_TXN , USER_SYNC_SLAVE_DATA
#else
#    define POLY_LTR559_TXN
#endif
// ROOT-CAUSE EXPERIMENT (split42, 2026-07-14): 3 dummy split transactions to grow
// NUM_TOTAL_TRANSACTIONS by 3 WITHOUT the pointing device — the one side effect of
// SPLIT_POINTING_ENABLE the heartbeat test never reproduced. With pointing the link
// establishes; without it 100% transport_fail. If these dummies (same resulting count
// as the working pointing build) revive the link, the bug is NUM_TOTAL_TRANSACTIONS-
// dependent in the transport. Only defined for split42 via -DPOLY_DUMMY_TXN_TEST.
#ifdef POLY_DUMMY_TXN_TEST
#    define POLY_DUMMY_TXN , USER_SYNC_DUMMY1, USER_SYNC_DUMMY2, USER_SYNC_DUMMY3
#else
#    define POLY_DUMMY_TXN
#endif
#define SPLIT_TRANSACTION_IDS_USER USER_SYNC_POLY_DATA, USER_SYNC_LAYER_DATA, USER_SYNC_LASTKEY_DATA, USER_SYNC_LATIN_EX_DATA, USER_SYNC_OVERLAY_DATA, USER_SYNC_COMPRESSED_DATA, USER_SYNC_ROI_DATA, USER_SYNC_DYNAMIC_KEYMAP_DATA, USER_SYNC_OVERLAY_MAP_DATA, USER_SYNC_FLASH_STAGE, USER_SYNC_RESET POLY_LTR559_TXN POLY_DUMMY_TXN

#define EE_HANDS

#define I2C_DRIVER I2CD0
#define I2C1_SCL_PIN GP0
#define I2C1_SDA_PIN GP1
#define I2C1_OPMODE OPMODE_I2C
#define I2C1_CLOCK_SPEED 400000

#define OLED_INIT

#define NOP_FUDGE 0.4

//This number can be calculated by dividing the MCU's clock speed
//by the desired SPI clock speed. For example, an MCU running at 8 MHz
//wanting to talk to an SPI device at 4 MHz would set the divisor to 2
#define SPI_DIVISOR (CPU_CLOCK / 10000000) // CPU_CLOCK is the LIVE clk_sys (RP_CORE_CLK), so
                                           // SPI stays at 10 MHz on any POLYKYBD_SYS_CLK


//only for v3 and later
//#define USB_VBUS_PIN GP24


// KEY_DISPLAYS_VBAT_PIN
//#define KEY_DISPLAYS_VBAT_PIN NO_PIN
//#define KEY_DISPLAYS_VDD_PIN NO_PIN

/* Reset. */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
//#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED GP17
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U

// Master to slave:
// ⚠️ This is a CAPACITY CAP, not a transfer size — transaction_rpc_exec() puts only
// the caller's `initiator2target_buffer_size` bytes on the wire, so raising it costs
// RAM in split_shared_memory_t and nothing per transaction.  It is also a SILENT
// ceiling: an oversized payload makes transaction_rpc_exec() `return false` before
// sending anything, and the bulk send_to_bridge() call sites discard the ack — so the
// halves just diverge with no log line.  96 fits the largest payload, latin_sync_t
// (90 B, asserted in state.h); the next largest is the 72 B doom mirror.
#define RPC_M2S_BUFFER_SIZE 96
// Slave to master: replies are a single ack byte, so this needs no headroom.
#define RPC_S2M_BUFFER_SIZE 72

// During fw_up the slave runs a deferred sector-by-sector erase (~50 ms per
// sector, 62+ sectors).  Each 50 ms window makes the slave's UART unresponsive,
// causing the split matrix transport to time out and increment connection_errors.
// The default threshold of 10 is reached in ~4 sectors (200 ms), triggering a
// 500 ms throttle that blocks all RPC calls.  Raise the threshold so the slave is
// never declared "disconnected" during the full erase sequence.
#define SPLIT_MAX_CONNECTION_ERRORS 200

// 2026-05-30: the FW_UP_CHUNK_NOOP_PROBE below (skip only fw_staging_write_chunk)
// streamed all 4458 chunks, proving the slave hang was INSIDE
// fw_staging_write_chunk: `space = FLASH_PAGE_SIZE(256) - s_buf_fill` was a
// uint8_t and truncated to 0 whenever the page buffer was empty (chunk 0 and
// every 256 B boundary), spinning the copy loop forever. Fixed in
// base/fw_staging.c (space/copy widened to uint32_t). Probe left here, disabled,
// for regression reference. See FW_UP_BASELINE.md (run 5).
// #define FW_UP_CHUNK_NOOP_PROBE

//######################################
//#          PolyKybd specific         #
//######################################
#define FW_VERSION "0.17.3"
// v2: adds GET_LANG_LIST_PACKED (cmd 27) — language list as 2-byte ISO index pairs.
// v3: SEND_OVERLAY_MAPPING (cmd 21) no longer ACKs per chunk — like every other
//     bulk overlay command (10, 16/17, 18/19) it is silent. The per-chunk ACK
//     carried no information (always ".", never read by the host) and arrived
//     only after the blocking UART bridge to the slave, leaving stale replies
//     that poisoned later commands' reads on the host.
// v4: adds GET/SET_IDLE_STYLE (cmd 28) — selects the idle (anti-burn-in) display
//     style (0 = legacy pulse, 1 = jitter). Persisted in EEPROM; host toggle.
// v5: SET_BRIGHTNESS (cmd 13) gains a flags byte (data[HID_DATA_IDX+1]):
//     VOLATILE (daylight/auto value, applied only in auto mode, never persisted)
//     + AUTO_ON / AUTO_OFF to engage/leave host-driven brightness. flags==0 is
//     the legacy persisted set, so older hosts keep working. The keyboard's
//     KC_DAUTO key toggles auto mode locally.
// v6: GET_ID (cmd 6) reply now carries a per-bundle font-pack version block after
//     the NUL-terminated id string — ['V'][count][u16 content_version × count] in
//     bundle order — so the host flashes only the font-pack bundles that are
//     missing/stale on the device. The pack is flashed per-bundle (FONTPACK_BEGIN
//     carries a bundle_id; each bundle has its own fixed flash slot).
// v7: adds GET/SET_OS (cmd 29 / 0x1d) — the active host-OS identity, a first-class
//     state DECOUPLED from the unicode mode (cmd 20). 0xFF queries (reply byte =
//     active OS, next byte = auto-mode flag), 0xFE engages auto mode, else sets the
//     OS (flags bit0: 1 = manual pin, 0 = host-auto push). Firmware also enables
//     QMK OS_DETECTION as the auto-mode fallback (only source on host-less / locked
//     machines). Persisted in poly_eeconf_t.os_state. Drives the modifier-legend
//     swap, the OS icon, and the semantic action keys. See enum poly_os in state.h.
// v8: distinguishes GNOME and KDE from generic Linux (new enum poly_os values
//     POLY_OS_LINUX_GNOME/KDE, set by the host from $XDG_CURRENT_DESKTOP and pushed
//     over cmd 29) so the GUI/Super-key hints + OS icon match the desktop environment.
//     Adds display-only shortcut-hint glyphs (word-nav, launcher, app/window switch,
//     close) to the symbol font-pack bundle (content_version bumped). No new command;
//     the host must match v8 to connect (exact-match gate) and reflash the bundle.
// v9: adds GET/SET_GLYPH_SCRIPT (cmd 30 / 0x1e) — a glyph-script OVERRIDE that
//     replaces the language-layer letter/digit legends with an alternative script
//     (0 = standard/off, 1 = Tengwar), leaving overlays and OS-hints untouched.
//     0xFF queries (reply byte = current script), else sets it; out-of-range NACKs.
//     Persisted in poly_eeconf_t.glyph_script; synced to the slave via poly_sync_t.
//     The Tengwar glyphs ship in a new "fantasy" font-pack bundle, which the host
//     flashes on connect. Host must match v9 to connect (exact-match gate).
// v10: glyph-script (cmd 30) becomes an OPEN-ENDED INDEX and ships 9 more scripts —
//     Elder Futhark runes, Aurebesh, Standard Galactic Alphabet, Cirth/Angerthas,
//     IBM VGA/CP437, Commodore 64, Amiga Topaz, APL, Braille (all in the regrown
//     "fantasy" bundle, content_version bumped). The wire format is unchanged (one
//     script byte); the semantic change is that the firmware now ACCEPTS ANY index
//     0..0xFE — an index it doesn't know, or whose font isn't flashed, just renders
//     the normal legend instead of NACKing. This DECOUPLES "add a font face" from the
//     protocol: within v10 the script set can grow freely (host offers more scripts
//     than a keyboard has; older keyboards degrade gracefully), so **adding scripts
//     never bumps the protocol again** — only a real wire/semantic change would.
//     0xFF stays the query sentinel. (v10 is the one-time bump that establishes this
//     open-ended contract, distinguishing it from the pre-v10 Tengwar-only firmware
//     that NACKed unknown indices.)
// v11: plain (uncompressed) overlay upload (cmd 10 / 0x0A) reframed. modifier and
//      segment now share ONE header byte — (segment << 4) | (modifier & 0x0F) — so
//      the header shrinks from 5 to 4 bytes and a full 60-byte segment fits the
//      64-byte report exactly. The old layout carried modifier and segment in
//      separate bytes, leaving only 59 bytes for the 60-byte segment, so the
//      firmware read 1 byte past the report (harmless on a no-MMU MCU, but the
//      last data byte of each segment was undefined). Compressed/ROI paths are
//      unchanged. Host must match v11 to connect (exact-match gate).
// v12: GUI (Cmd) COMBINES with the other modifiers for overlays. The modifier
//      variant is now simply the L/R-folded QMK modifier bitmask 0..15 (bit0 Ctrl,
//      bit1 Shift, bit2 Alt, bit3 GUI), so Cmd+Shift, Cmd+Alt, Cmd+Shift+Alt, ...
//      each address their own overlay instead of collapsing onto the bare-GUI
//      variant 8 (see adjust_overlay_idx_to_mod in fill_overlay.c). The upload
//      commands are UNCHANGED (all three already carry the modifier in a 4-bit
//      field), but the flat (slot, variant) index space grows 810 -> 1440, which
//      no longer fits the 10-bit fields of SEND_OVERLAY_MAPPING (cmd 21).
//      cmd 21 STAYS FIXED AT 10 BITS — unchanged, and still the only mapping
//      command a pre-v12 keyboard understands. The wider space rides a NEW
//      command, SEND_OVERLAY_MAPPING_W (cmd 33), whose data[2] carries the value
//      width: the host groups mapping pairs by the width they need (8/9/10/11 ->
//      30/27/24/22 pairs per report) and sends each group at its own width. Since
//      variants 0..10 still fit 10 bits, the common case keeps cmd 21's density;
//      only indices >= 1024 need 11. A pre-v12 keyboard gets cmd 21 only, and
//      never a variant > 8 (it has no index space for one).
//  v13 GET/SET_GLYPH_SIZE (cmd 34 / 0x22): the size of a key's MAIN legend —
//      0 small (the original 27 px face), 1 medium, 2 large. 0xFF queries. The
//      shift / AltGr previews and every other kind of chrome are deliberately
//      unaffected: a keycap has room for one big thing. Persisted in
//      poly_eeconf_t.glyph_size and synced via poly_sync_t.glyph_size.
//      ⚠️ Unlike the glyph SCRIPT (cmd 30), the range is CLOSED and an unknown
//      value NACKs — see the case-34 comment for why an open range is right for
//      a catalogue of faces and wrong for a size. The bigger faces are latin
//      only and live in the `latinbig` font-pack bundle; without it (or for a
//      non-latin legend) the render falls back to small, so the setting is
//      always safe to accept.
#define PROTOCOL_VERSION 15

#define FULL_BRIGHT 50
#define MIN_BRIGHT 1
#define DISP_OFF 0
#define BRIGHT_STEP 1

//10 sec
#define FADE_TRANSITION_TIME 10000
//2 min
#define FADE_OUT_TIME 120000
//10 min
#define TURN_OFF_TIME 1200000

// Idle "jitter" style: while pulsing, each key independently relocates its own legend
// to a fresh random spot the moment that key's out-of-phase pulse dims it to black
// (kdisp_idle), so the lit pixels migrate and don't burn in — keys roam individually
// rather than in lockstep. There is deliberately NO global offset envelope: the travel
// range is derived per glyph from its own on-screen slack (roll_idle_offset), so a slim
// "i" roams its full free width while a wide "w" or a full-width CJK legend moves only
// as far as it can without clipping. A fixed ±N cap would be counter-productive here
// (it would throttle the slim glyph and edge-bias the wide one).
// How often a key relocates: the breathing curve dips dark ~twice per ~15 s pulse
// cycle, so we'd otherwise move each key ~every 7.5 s. Relocate only every Nth dark
// episode to slow the drift (3 -> ~every 22 s per key). Raise for a calmer display.
#define IDLE_JITTER_PERIOD 3

// Overlay-burst coalescing (sync_and_refresh_displays + base/update.c). A program
// switch arrives as a burst of overlay/mapping HID reports; each would otherwise
// trigger a full ~50-100 ms keycap re-render of half-staged state (measured ~12 per
// switch). A fresh render is deferred while the burst is still arriving, and fires on
// whichever comes first:
//   QUIET_MS   - no overlay command for this long: the burst settled, render once.
//   FLUSH_COUNT- this many overlay commands piled up: flush a render mid-burst so a
//                LONG transfer stays reactive (the keys visibly fill in) instead of
//                showing nothing until the whole thing lands. This is the key knob
//                for perceived latency: lower = snappier + more (smaller) renders,
//                higher = fewer renders + longer delay before the first appears.
//   MAX_MS     - hard cap on the total hold (backstop for a dense trickle that never
//                reaches FLUSH_COUNT and never goes quiet).
// Deferring keeps the loop responsive to keystrokes (the whole point); only the
// VISIBLE swap is delayed. Tune against the LoopProf "ovltot" line on hardware.
#define OVERLAY_COALESCE_QUIET_MS   12
#define OVERLAY_COALESCE_FLUSH_COUNT 8
#define OVERLAY_COALESCE_MAX_MS     120

//######################################
//#          Overlays specific         #
//######################################
#define HID_REPORT_SIZE RAW_EPSIZE // now 64, but might change

// segement * bytes = 360 (=2880/8 => all bits of an overlay)
#define NUM_SEGMENTS_PER_OVERLAY 6
#define BYTES_PER_SEGMENT 60

#define HID_DATA_MAX (HID_REPORT_SIZE-2) // minus via cmd byte and minus polybybd cmd byte -> -2
#define COMPRESSED_MAX HID_DATA_MAX
#define COMPRESSED_START (HID_REPORT_SIZE-4) // additional minus keycode and minus modifier -> -4

#define ROI_MAX (HID_REPORT_SIZE-2) // minus via cmd byte and minus polybybd cmd byte -> -2
#define ROI_START (HID_REPORT_SIZE-7) // additional minus keycode and 4 bytes compressed roi header -> -7

#define NUM_OVERLAYS 90
// The modifier variant IS the L/R-folded QMK modifier bitmask (bit0 Ctrl, bit1
// Shift, bit2 Alt, bit3 GUI), so all 16 combinations are addressable and the
// numbering is self-describing: 0 = none, 1 = Ctrl, 2 = Shift, 3 = Ctrl+Shift,
// 4 = Alt, ... 8 = GUI, 9 = GUI+Ctrl, 10 = GUI+Shift, ... 15 = GUI+Ctrl+Alt+Shift.
// Protocol v12 grew this from 9 (GUI could not be combined — every GUI+x chord
// collapsed onto the bare-GUI variant 8, so e.g. a Mac Cmd+Shift+P shortcut drew
// the Cmd image). ⚠️ Growing it further would need OVERLAY_MAP_IDX_BITS to grow
// too — see the note there.
#define NUM_VARIATIONS_WITH_MAP 16

// Physical overlay pool: how many DISTINCT 360-byte keycap images can be resident
// at once. Deliberately DECOUPLED from NUM_OVERLAYS*variants — overlay mapping is
// mandatory, so display_to_pool[] (810 entries, every keycode-slot x variant pair)
// points each pair at any pool slot. Variants that share artwork therefore cost
// ONE slot, not one each: measured across the 24 shipped app templates, the
// heaviest app needs 62 distinct images and the median 31, so 600 holds ~10 apps
// at worst case. It was previously NUM_OVERLAYS*7 = 630 purely because the array
// was indexed directly by (slot, variant).
// ⚠️ Lower bound is set by DOOM, not by overlays: the pool IS the game arena
// (see base/overlay.c + doom/doom_arena.h), whose floor is ~205 KB / 584 slots.
// ⚠️ Mirrored in ld/RP2040_FLASH_TIMECRIT_DOOM.ld, ld/RP2040_FLASH_TIMECRIT_DOOMPACK.ld,
// doom/pack/build_pack.sh (RAM_SIZE) and PolyKybdHost device_settings.py — all five
// must move together, and the pack's .plyx carries the size in its header.
#define NUM_OVERLAY_SLOTS 600
#define OVERLAY_MAP_IDX_CNT (NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP)
// --- overlay-mapping wire widths -------------------------------------------
// A mapping report is a flat LSB-first bit stream of equal-width values read as
// alternating `from, to, from, to, …`. `from` is a display position (a flat
// (slot, variant) index, 0..OVERLAY_MAP_IDX_CNT-1) and `to` is a pool slot
// (0..NUM_OVERLAY_SLOTS-1), so the width a given pair NEEDS is
// max(bits(from), bits(to)).
//
// Two commands carry that stream:
//   cmd 21 SEND_OVERLAY_MAPPING    — fixed 10 bits. Unchanged since forever, and
//                                    the only one a pre-v12 keyboard understands.
//   cmd 33 SEND_OVERLAY_MAPPING_W  — v12+. data[2] is the WIDTH, data[3..] the
//                                    stream, so the host can pick the narrowest
//                                    width each group of pairs fits in.
// Pairs are order-independent (each is a standalone assignment), so the host
// partitions them BY REQUIRED WIDTH rather than by index order: variants 0..10
// stay in the 10-bit form (24 pairs/report, no regression), only the high GUI
// combos need 11, and low-index pairs can ride at 9 or even 8 (27 / 30 pairs).
// 11 is the ceiling — max `from` is 1439 < 2048 — so 12+ is never needed today.
#define OVERLAY_MAP_IDX_BITS 10                 // cmd 21's fixed width
#define OVERLAY_MAP_IDX_CNT_PER_REPORT (HID_DATA_MAX*8/OVERLAY_MAP_IDX_BITS)
// cmd 33: one extra header byte for the width, so one fewer data byte.
#define OVERLAY_MAP_W_HDR   3                   // report id + cmd + width
#define OVERLAY_MAP_W_BYTES (HID_REPORT_SIZE-OVERLAY_MAP_W_HDR)
// Widths the DECODER accepts. Deliberately wider than the 8..11 the host emits
// today: the decoder is width-generic, and every value it produces is still
// range-checked against OVERLAY_MAP_IDX_CNT / NUM_OVERLAY_SLOTS before it can
// touch a table, so a stream at 12..16 can only ever be rejected pair-by-pair —
// it cannot address anything a narrower one couldn't. Accepting them keeps
// headroom for a future index-space growth without a second command, in the same
// spirit as the open-ended glyph-script index (v10). ⚠️ The bound that actually
// protects memory is the per-value range check, NOT this range; both the read and
// the write side touch a byte only when the value truly extends into it, verified
// by round-tripping the packer through the decoder across all of 8..16.
#define OVERLAY_MAP_WIDTH_MIN 8
#define OVERLAY_MAP_WIDTH_MAX 16
// Values a stream of `bytes` bytes holds at `width` bits — the ONE definition
// host and firmware must agree on, since there is no count field: the host fills
// every value (padding by repeating the last pair, which is idempotent) so a
// disagreement would decode trailing junk as real mappings.
#define OVERLAY_MAP_VALUES(bytes, width) ((uint16_t)((bytes)*8/(width)))
// Width the master's own repair packer uses — the widest, so any index fits.
#define OVERLAY_MAP_REPAIR_WIDTH 11
#define UNSET_OVERLAY_MAPPING 0xffff

#define PICO_FLASH_SIZE_BYTES (8 * 1024 * 1024)

// ⚠️ The wear-levelling EEPROM backing store lives at the TOP of physical flash:
// the rp2040_flash driver places it at PICO_FLASH_SIZE_BYTES - WEAR_LEVELING_BACKING_SIZE
// (0x7FE000..0x800000 here), i.e. INSIDE our resource region — see the flash map in
// base/fw_staging.h, where FW_DOOMPACK_SLOT_SIZE subtracts it. It is pinned here
// rather than inherited from the driver default so that (a) the reservation is
// visible beside the flash size it is carved out of, and (b) changing it is a
// deliberate act that the _Static_assert in fw_staging.c re-checks against the
// DoomPack slot. Raising it grows the store DOWNWARD into that slot.
#define WEAR_LEVELING_BACKING_SIZE 8192

#define OLED_FONT_START	32
#define OLED_FONT_END	126
#define OLED_FONT_H "base/fonts/base_font.h"
#define OLED_BRIGHTNESS 60
#define OLED_DISABLE_TIMEOUT
#define OLED_UPDATE_INTERVAL 66 //15fps

#define MOUSEKEY_MOVE_DELTA	2

#define USE_CORE1

#define DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT 8

// Reclaim the storage QMK reserves for the layers we never store.
//
// DYNAMIC_KEYMAP_LAYER_COUNT must stay 12 (QMK asserts it is >= the compiled layer
// count, keymap_introspection.c), but only layers 0..7 are ever READ or WRITTEN from
// EEPROM -- everything from _SL up is served straight out of flash by poly_keycode_at().
// QMK's default addresses put the encoder map and the macro buffer after all TWELVE
// layers, so 640 B (split72) / 384 B (split42) of keymap plus a further 32 B of encoder
// map sit there addressed by nothing. Basing both on the write cap instead hands that
// space to the macro buffer, which is the one region sized by "whatever is left".
//
// The bodies are expanded at the USE site (nvm_dynamic_keymap.c), not here, so
// MATRIX_ROWS/COLS and NUM_ENCODERS do not have to be defined yet -- exactly how QMK's
// own defaults are written.
//
// WARNING: this only works because nothing writes layers >= the cap. Two guards keep
// that true and BOTH are load-bearing: the host cannot (dynamic_keymap_set_keycode_poly
// / _set_buffer_poly clamp to the cap), and OUR reset walks the cap rather than
// DYNAMIC_KEYMAP_LAYER_COUNT. QMK's own dynamic_keymap_reset() does NOT -- it loops to
// DYNAMIC_KEYMAP_LAYER_COUNT and calls nvm_dynamic_keymap_update_keycode(), whose bound
// check is also DYNAMIC_KEYMAP_LAYER_COUNT, so it writes layers 8..11 straight over the
// encoder map and the macro buffer. It is still reachable from eeconfig_init_quantum();
// eeconfig_init_kb() repairs after it (see poly_keymap.c). Do not add a third call site.
#define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR \
    (DYNAMIC_KEYMAP_EEPROM_ADDR + (DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2))
#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR \
    (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR + (DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT * NUM_ENCODERS * 2 * 2))

// --- Macro labels -----------------------------------------------------------
// The label a macro keycap spells out along its bottom edge. Stored as a FIXED-STRIDE
// array carved off the top of the macro region, NUL-padded, deliberately NOT inside
// the NUL-delimited body buffer: a body is addressed by counting separators from the
// start, which is fine once per keypress and wrong for something the render path reads
// for every keycap on every display refresh. Fixed stride makes label(n) a multiply.
//
// Shrinking DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE is what keeps the two apart -- every QMK
// path that touches the body bounds itself on that constant, so nothing upstream can
// reach the labels even though they sit in the same region.
//
// 12 bytes is the measured average that fits the 72 px panel in the _Nano_ 10 px face
// (8 in the worst case, all-W; 24 of narrow letters). Truncation is by PIXEL WIDTH at
// render time, not by this length -- see poly_macro_label_fit().
#define POLY_MACRO_LABEL_LEN   12
#define POLY_MACRO_COUNT       16
#define DYNAMIC_KEYMAP_MACRO_COUNT POLY_MACRO_COUNT

// A macro OWNS its keycap -- it cannot be folded into a modifier or a tap-hold, because
// QMK carries the wrapped key in the low byte (MT/LT are `(kc) & 0xFF`) and a macro
// keycode is 0x7700+. Since the whole cell is the macro's, the cell is free to be more
// than a legend, so each record carries HOW to draw it alongside the text:
//
//   style  1 B   POLY_MACRO_STYLE_*
//   icon   4 B   codepoint drawn above the caption, 0 = none. Four bytes because the
//                interesting glyphs are emoji at 0x1F300+, well past 16 bits.
//   text  12 B   the caption
//
// One record, one address, one dirty bit, one bridge -- deliberately NOT a parallel
// array beside the labels, which is the shape that goes out of step and then needs a
// guard to remember it (see the enumerating-guard note in CLAUDE.md). The appearance
// cannot arrive half-applied on the slave because it never travels in two pieces.
#define POLY_MACRO_ICON_LEN    4
#define POLY_MACRO_LOOK_LEN    (1 + POLY_MACRO_ICON_LEN + POLY_MACRO_LABEL_LEN)
#define POLY_MACRO_LABEL_BYTES (POLY_MACRO_COUNT * POLY_MACRO_LOOK_LEN)

// QMK derives this inside nvm_dynamic_keymap.c, i.e. it exists in exactly one
// translation unit. Everything below (and poly_macro.c) needs the same number, so
// define it here -- QMK's own #ifndef then picks ours up and the two cannot disagree.
#ifndef DYNAMIC_KEYMAP_EEPROM_MAX_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR (TOTAL_EEPROM_BYTE_COUNT - 1)
#endif

#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE \
    ((DYNAMIC_KEYMAP_EEPROM_MAX_ADDR - DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + 1) - POLY_MACRO_LABEL_BYTES)
#define POLY_MACRO_LABEL_ADDR \
    (DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE)
