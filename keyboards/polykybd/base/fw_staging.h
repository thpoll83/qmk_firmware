// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ── PolyKybd RP2040 flash map (8 MB external QSPI) ───────────────────────────
// All offsets are from the start of physical flash (XIP_BASE + offset = XIP addr):
//
//   0x000000 .. 0x200000   running firmware         (linker flash1 XIP = 2 MB)
//   0x200000 .. 0x400000   firmware-update staging  (4 KB header + staged image)
//   0x400000 .. 0x7FE000   resource / overlay data  (FW_RESOURCE_OFFSET)
//   0x7FE000 .. 0x800000   EEPROM  (wear-levelling backing store, NOT ours)
//
// ⚠️ That last row is the one nothing used to declare. QMK's rp2040_flash
// wear-levelling driver puts the emulated EEPROM at the TOP of physical flash
// (PICO_FLASH_SIZE_BYTES - WEAR_LEVELING_BACKING_SIZE), which lands INSIDE the
// resource region — so the resource region is 4 MB minus that reservation, not
// a clean 4 MB. Everything a user configures lives there: the dynamic keymap,
// brightness, language, idle style, glyph script, the Intl assignment map.
//
// FW_STAGING_OFFSET is kept equal to the linker's flash1 length (2 MB — see
// RP2040_FLASH.ld, FLASH_LEN default 2048k) so the firmware can NEVER grow into
// the staging area: a build over 2 MB fails to LINK instead of silently
// overwriting a staged update. FW_UP_MAX_SIZE keeps the 4 KB staging header +
// image below the resource region. These invariants are checked by
// _Static_assert in fw_staging.c.
//
// Raised from a 1 MB firmware / 1 MB staging split to 2 MB / 2 MB on 2026-06 as
// the image neared the old 1 MB boundary (the previously-unused 0x200000-0x400000
// window became the staging area; resources stay at 4 MB).
#define FW_STAGING_OFFSET      0x200000UL      // staging starts at 2 MB (== linker flash1 length)
#define FW_STAGING_HEADER_SIZE 0x001000UL      // first 4 KB of staging = header sector
#define FW_STAGING_DATA_OFFSET (FW_STAGING_OFFSET + FW_STAGING_HEADER_SIZE)
#define FW_RESOURCE_OFFSET     0x400000UL      // resource/overlay region (== FLASH_TARGET_OFFSET in keymap.c)
#define FW_UP_MAX_SIZE         0x1FF000UL      // max staged image: 2 MB staging region minus the 4 KB header
#define FW_STAGING_MAGIC       0xD1F1A51BUL

// Bytes per firmware-update chunk (HID and split-RPC payload).
// 56 bytes fits in the 64-byte RAW_EPSIZE HID packet (2 bytes framing + 4 bytes offset + 56 data + 2 spare).
// The split RPC struct is 4 CRC + 4 offset + 56 data = 64 bytes < RPC_M2S_BUFFER_SIZE 72 bytes.
#define FW_UP_CHUNK_SIZE 56

// FW-2 physical-presence confirmation. Under FW_REQUIRE_SIGNATURE an image that
// is not validly signed is not refused outright: COMMIT raises a prompt ON THE
// KEYCAPS (a big A/ACCEPT on the left half, R/REJECT on the right) and waits for
// a keypress. The acknowledgement is deliberately NOT carried over HID — signing
// defends against any process that can talk the flash protocol, and such a
// process could forge a reply on that same channel. A keypress cannot be produced
// remotely, so the answer has to come off the matrix.
//
// How long the prompt stays up. Long enough to read it, walk to the keyboard and
// decide; short enough that an unattended board falls back to refusing. RAM-only,
// and re-armed per flash — a reboot or a new BEGIN always clears it.
#define FW_CONFIRM_WINDOW_MS 60000u

// True while the prompt is up: COMMIT answers '?' and the keycaps show A/R.
bool fw_staging_awaiting_confirm(void);
// True from the moment the prompt goes up until the answer is consumed by the
// COMMIT that acts on it. COMMIT uses this to skip re-bridging to the slave while
// the host re-polls: the slave committed on the first COMMIT and re-running its
// finalize would re-erase the staging header sector once per poll.
bool fw_staging_confirm_in_progress(void);
// The physical answer, from process_record_user on the master. Idempotent — only
// the first call while pending decides.
void fw_staging_confirm_answer(bool accept);
// Times the prompt out (-> reject). Call once per housekeeping pass.
void fw_staging_confirm_tick(void);

// True when the last fw_staging_finalize() refused the image because it was not
// validly signed (rejected or timed out at the prompt), as opposed to a CRC
// mismatch. Lets COMMIT answer with a distinct status so the host and the HIL rig
// can report the real reason instead of guessing — they are indistinguishable
// from the bool alone.
bool fw_staging_refused_unsigned(void);

// Must be called once before any other fw_staging_* function.

// Hold core1 out of flash for a caller doing its own flash work (EEPROM flush).
// See the comment on the definition: QMK's wear-levelling backing store erases with
// core1 running, which is only survivable while the window stays small.
void fw_staging_core1_lockout_begin(void);
void fw_staging_core1_lockout_end(void);

// Re-CRC the staged image AS IT SITS IN FLASH. COMMIT only checks the bytes as they
// arrived in RAM, so it says nothing about what actually landed -- and the applier is
// about to erase the only working firmware on the strength of it.
bool fw_staging_verify_staged_flash(uint32_t *size, uint32_t *expect_crc, uint32_t *actual_crc);

// Disarm an armed apply without applying it.
void fw_staging_cancel_apply(void);

// Whether the LAST self-apply (this boot or any earlier one, until the next BEGIN
// erases the record) finished its copy. Recorded in flash rather than the watchdog
// scratch because the case that matters is a half that never came back: the scratch
// dies with the power, this does not.
bool fw_staging_last_apply_completed(uint32_t *sectors, uint32_t *size, uint32_t *psm_spins);

// Where the last self-apply's copy first disagreed with its source, checked by the
// applier itself before it reset. 0xFFFFFFFF = the written image matched the staged
// one exactly, so the copy is not the problem.
bool fw_staging_last_apply_diff(uint32_t *offset, uint32_t *got, uint32_t *want);

// How far the last self-apply's copy actually got, from the per-sector log it writes
// into staging. Survives the power cycle a BOOTSEL recovery needs, which the watchdog
// scratch does not -- and a copy that stops partway leaves no other trace at all.
// How far the last self-apply's sector copy got, read back from the in-flash progress
// log. The copy runs top-down (N-1 .. 1, then 0), so *lowest is the sector it stopped
// at; false means no sector completed at all.
bool fw_staging_apply_progress(uint32_t *lowest, uint32_t *highest, uint32_t *count);

// True when the last self-apply erased its log and marked it started, i.e. it reached
// the copy. Separates "died before the first sector" from "died erasing it".
bool fw_staging_apply_started(void);

// Bracket markers around the first image sector the copy touched: how far into that
// one sector it got (1 = about to erase, 2 = erase returned, 3 = first page written),
// which flash offset, and the sector's first word read back at stages 2 and 3.
bool fw_staging_apply_marks(uint32_t *stage, uint32_t *addr, uint32_t *erased, uint32_t *progd, uint32_t *srcw);

void fw_staging_init(void);

// What the LAST self-apply managed before this boot, read out of the watchdog scratch
// registers (they survive a watchdog reset, not a power cycle) and cleared at init.
// Returns false when no apply preceded this boot. The apply routine cannot log: the
// console is gone, interrupts are off and it never returns -- so this is how a half
// that came back reports how far its copy got.
bool fw_staging_apply_breadcrumb(uint32_t *last_sector, bool *completed, uint32_t *psm_spins);

// Staging target: which flash region a begin/chunk/finalize sequence writes to,
// and how finalize completes. FIRMWARE stages to the 2 MB firmware-update region
// (header sector + image) and finalize stamps the staging header for a later
// apply+reboot. FONTPACK writes the "PlyF" font pack in place at the resource
// region (no header sector) and finalize re-loads the fonts — no reboot.
// DOOMWAD writes the doom easter egg's WHX game data in place at its fixed
// resource slot (the engine's XIP TINY_WAD_ADDR) and finalize just validates
// the "IWHX" magic; the data is only ever read after game mode boots its
// engine. DOOMPACK writes the executable engine pack ("PlyX", see
// doom/PACK_DESIGN.md) in place at the top of the resource region; finalize
// does the O(1) header sanity checks (magic/ABI/size) — the full image CRC is
// the loader's job at game entry, where a bad pack safely degrades to the
// fire demo. The streaming / paging / deferred-erase / slave-bridge machinery
// is identical for all targets — only the base offset, the header sector, and
// the finalize action differ.
typedef enum {
    FW_TARGET_FIRMWARE = 0,
    FW_TARGET_FONTPACK = 1,
    FW_TARGET_DOOMWAD  = 2,
    FW_TARGET_DOOMPACK = 3,
} fw_target_t;

// Bytes at the top of flash that belong to the wear-levelling EEPROM, NOT to us.
// Must equal WEAR_LEVELING_BACKING_SIZE (pinned in keyboards/polykybd/config.h);
// fw_staging.c _Static_asserts the two against each other. Defined here as a
// literal rather than referencing that macro so this header stays self-contained
// for the host-side unit tests, which include it without QMK's config.h.
#define FW_EEPROM_RESERVE_SIZE 0x002000UL      // 8 KB

// The doom slots, expressed like fontpack slots (offsets relative to
// FW_RESOURCE_OFFSET). The upper 2 MB of the resource region is split:
//   flash 0x600000..0x7BFFFF (1.75 MB)  WHX game data — matches the engine's
//                                       XIP TINY_WAD_ADDR 0x10600000; the
//                                       current doom1.whx (1,800,344 B) fits
//                                       with ~35 KB headroom.
//   flash 0x7C0000..0x7FDFFF (248 KB)   DoomPack (PlyX header + engine image,
//                                       211,384 B measured) — XIP 0x107C0000.
//   flash 0x7FE000..0x7FFFFF ( 8 KB)    EEPROM — see FW_EEPROM_RESERVE_SIZE.
//
// ⚠️ The DoomPack slot STOPS SHORT of the end of flash, and that is load-bearing.
// It used to be declared as the full 256 KB to 0x800000, which overlapped the
// EEPROM backing store exactly. Nothing caught it because the pack is 211 KB and
// only the sectors an image actually needs are erased — but fw_staging_finalize()
// accepts any image up to slot_size - 64, so a pack that grew into its own
// declared slot would have erased the user's keymap and settings as a side
// effect, with no diagnostic. Deriving the size keeps the two provably disjoint.
#define FW_DOOMWAD_SLOT_OFF   0x200000UL
#define FW_DOOMWAD_SLOT_SIZE  0x1C0000UL
#define FW_DOOMPACK_SLOT_OFF  0x3C0000UL
#define FW_DOOMPACK_SLOT_SIZE (0x040000UL - FW_EEPROM_RESERVE_SIZE)   // 248 KB

// On-wire pseudo bundle ids (CMD_FONTPACK_BEGIN data[10]) selecting the
// DOOMWAD / DOOMPACK targets — the doom installs ride the font-pack HID flow.
// Mirrored by the host (PolyKybdHost hid_fontpack.py DOOMWAD_BUNDLE_ID /
// DOOMPACK_BUNDLE_ID); real font bundle indices stay far below them.
#define FONTPACK_BUNDLE_DOOMWAD  0x7Fu
#define FONTPACK_BUNDLE_DOOMPACK 0x7Eu

// Synchronous begin (master / USB side): erases staging flash sector-by-sector
// with interrupts briefly re-enabled between sectors.  Blocks for ~50 ms per
// sector (ceil(image_size/4096) + 1 sectors total).
void fw_staging_begin(uint32_t image_size, uint32_t image_crc);              // FIRMWARE target
void fw_staging_begin_target(uint32_t image_size, uint32_t image_crc, uint8_t target);

// Deferred begin (slave side): stores parameters and schedules the erase.
// Returns immediately — safe inside a split-link transaction handler.
// Call fw_staging_process_deferred() from housekeeping_task_user() to drive the erase.
void fw_staging_begin_deferred(uint32_t image_size, uint32_t image_crc);     // FIRMWARE target
void fw_staging_begin_deferred_target(uint32_t image_size, uint32_t image_crc, uint8_t target);

// FONTPACK target only: select the bundle slot (offset relative to FW_RESOURCE_OFFSET
// + reserved size) the next begin/chunk/finalize sequence writes to. Must be called
// before the FONTPACK begin on BOTH halves (master + slave). No effect on FIRMWARE.
void fw_staging_set_fontpack_slot(uint32_t slot_off, uint32_t slot_size);

// Active FONTPACK bundle slot offset (relative to FW_RESOURCE_OFFSET) of the
// in-flight flash — for the on-screen bundle-name label. Pair with
// fontpack_slot_name(). Only meaningful while the active target is FW_TARGET_FONTPACK.
uint32_t fw_staging_fontpack_slot_off(void);

// Erases one staging sector per call.  Call from housekeeping_task_user()
// until fw_staging_write_chunk() no longer returns false for offset==0.
void fw_staging_process_deferred(void);

// Write one chunk of sequential firmware data to staging.
// `offset` must equal the cumulative bytes already written; returns false on error.
bool fw_staging_write_chunk(uint32_t offset, const uint8_t *data, uint8_t len);

// Flush any partial last page, verify full CRC32 of staged data, write header magic.
// Returns true on CRC match; sets commit-pending flag on success.
bool fw_staging_finalize(void);

// Slave-side finalize: like fw_staging_finalize() but defers the heavy FONTPACK
// reload (full-body CRC + reassemble) out of the split-transaction callback to
// housekeeping, so the ~50 ms verify can't overrun the ~20 ms transaction window.
// ACKs on the O(1) transport CRC (byte-identity with the master's verified pack).
bool fw_staging_finalize_defer_reload(void);

// Drain the deferred FONTPACK reload (call from housekeeping_task_user()).
// Returns true iff it reloaded this call, so the caller can refresh the display.
bool fw_staging_process_fontpack_reload(void);

// True while the deferred sector-by-sector erase (fw_staging_begin_deferred) is still running.
bool fw_staging_erase_pending(void);

// True from fw_staging_begin/fw_staging_begin_deferred until fw_staging_finalize() returns
// (success or failure).  Use this to suppress EEPROM saves during a fw_up to avoid a
// wear-leveling compact operation (backing_store_erase = 100 ms IRQ-disabled) racing
// with incoming FW_UP_CHUNK transactions and exhausting all UART retries.
bool fw_staging_fw_up_active(void);

// 0xFF when idle, else the FW_TARGET_* of the in-progress flash (FIRMWARE/FONTPACK),
// for an on-screen "updating …" label. fw_staging_image_size() is the total image
// size for a progress bar (0 when idle).
uint8_t  fw_staging_active_target(void);
uint32_t fw_staging_image_size(void);

// Current write cursor (bytes accepted into staging).  This half's "next
// expected chunk offset" — the slave echoes it in fw_up_chunk_reply_t and the
// master reports the lower of the two halves' cursors to the host in a chunk
// NACK so the updater can rewind and resync (see hid_fw_up.c).
uint32_t fw_staging_next_offset(void);

// True if any chunk data has been written to staging since the last fw_staging_begin/fw_staging_begin_deferred.
// Used by the slave handler to detect partial writes from a previous failed attempt
// that require re-erasing staging before accepting new chunks.
bool fw_staging_written(void);

// True after a successful fw_staging_finalize(); cleared after fw_staging_apply_and_reboot() is called.
bool fw_staging_commit_pending(void);

// Return firmware image size (from linker symbols).
uint32_t fw_staging_get_own_fw_size(void);

// Compute CRC32 of the running firmware image (slow; avoid calling on every boot).
uint32_t fw_staging_get_own_fw_crc(void);

// Return pointer to start of active firmware in flash (XIP address of __flash_binary_start).
const uint8_t *fw_staging_get_fw_base(void);

// Copy staging area → active firmware region, then hard-reset.  NEVER RETURNS.
// No-op (returns normally) if no valid staged image is found.
void fw_staging_apply_and_reboot(void);

// True if the staging header sector holds a valid, applyable image
// (magic present, size in range).  Written by fw_staging_finalize() on CRC match.
bool fw_staging_has_valid_staged_image(void);

// ── Firmware image signature (FW-2) ─────────────────────────────────────────
// Ed25519 signature length over the staged firmware image.
#define FW_SIG_LEN 64

// Provide the detached Ed25519 signature for the image being staged (FIRMWARE
// target). Set by the host's CMD_FW_UP_SIGNATURE before COMMIT. fw_staging_finalize()
// then verifies it (master only) against FW_SIGNING_PUBKEY in fw_pubkey.h:
//   - Phase A (default): verify + log only; the result does NOT block COMMIT.
//   - With FW_REQUIRE_SIGNATURE defined: a missing/invalid signature fails COMMIT.
// Reset to "absent" on every fw_staging_begin*(). No-op for the FONTPACK target.
void fw_staging_set_signature(const uint8_t sig[FW_SIG_LEN]);

// Arm the deferred apply: housekeeping_task_user() will then run
// fw_staging_apply_and_reboot() once.  Use only after a successful COMMIT
// (fw_staging_has_valid_staged_image() == true).  PHASE 2: master only.
void fw_staging_arm_apply(void);

// Arm a deferred plain reboot (QK_REBOOT slave path): the slave's
// housekeeping_task_user() then calls mcu_reset() once.  Unlike
// fw_staging_arm_apply(), this does NOT install the staged image — it just
// reboots, so both halves can restart together when the master reboots.
void fw_staging_arm_reboot(void);
bool fw_staging_reboot_pending(void);

// ---------------------------------------------------------------------------
// Diagnostic snapshot — populated by the slave's handlers so the master can
// query "what does the slave think happened" after a failed FW_UP_CHUNK.
// ---------------------------------------------------------------------------
typedef struct _fw_staging_status_t {
    uint8_t  initialized;
    uint8_t  fw_up_active;
    uint8_t  erase_pending;
    uint8_t  last_chunk_ack;        // last value returned from chunk handler (SYNC_ACK / SYNC_CRC32_ERR / SYNC_NACK_REFUSED)
    uint16_t erase_sector_next;
    uint16_t erase_sector_count;
    uint32_t next_offset;            // s_next_offset (bytes accepted)
    uint32_t last_chunk_offset;      // offset arg of most recent chunk
    uint16_t begin_handler_calls;
    uint16_t chunk_handler_calls;
    uint16_t chunk_handler_errors;   // chunks that hit CRC mismatch / write fail
    uint16_t process_deferred_calls; // times fw_staging_process_deferred() entered
    uint16_t process_deferred_advances; // times a sector was actually erased
    // Last ack the slave's COMMIT handler returned (0 = it has never run one).
    // This is what lets the master tell a REFUSAL from a lost acknowledgement when
    // the COMMIT reply itself goes missing: fw_up_active is cleared by finalize
    // either way, so it cannot distinguish them, but this records the verdict.
    // Consumes half the old pad, so the struct size — and therefore the RPC reply
    // size — is unchanged.
    uint8_t  last_commit_ack;
    uint8_t  pad;
} fw_staging_status_t;

// Fill `out` with the current internal state.  Safe to call from anywhere.
void fw_staging_get_status(fw_staging_status_t *out);

// Counter / state mutators used by the split handlers in split_fw_up.c.
void fw_staging_note_begin_call(void);
void fw_staging_note_chunk_call(uint32_t offset, uint8_t ack);
// Record the ack the slave's COMMIT handler is about to return, so a later STATUS
// probe can report the verdict even if that reply never reached the master.
void fw_staging_note_commit_ack(uint8_t ack);

// Diagnostic helper used while bisecting the fw_up slave-hang bug
// (see FW_UP_DEBUG_NOTES.md): set the s_fw_up_active flag without
// performing the staging erase / core1 halt.  Lets the master act as a
// pure relay (master never touches its own staging) so we can isolate
// whether the failure is in the staging code or in the split transport.
// Must be paired with a `false` call to clear when the relay-only flow
// ends.
void fw_staging_set_fw_up_active(bool active);
