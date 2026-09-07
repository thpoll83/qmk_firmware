// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"

#include "fw_staging.h"
#include "fontpack.h"        // FONTPACK target: fontpack_reload()/present + max size
#include "polymod_crc32.h"
#include "monocypher-ed25519.h"   // FW-2: Ed25519 image signature verify (polymod_monocypher)
#include "fw_pubkey.h"                    // FW-2: FW_SIGNING_PUBKEY (image signing key)
#include "crash_record.h"                 // crash_watchdog_stop() before the self-apply

#include "hardware/flash.h"
#include "hardware/sync.h"

#include <string.h>

// Flash-partition invariants (see the map in fw_staging.h). Compile-time guards
// so any future offset change that would let the regions overlap fails the build
// rather than corrupting a staged update or the resource region at run time.
_Static_assert(FW_UP_MAX_SIZE <= FW_STAGING_OFFSET,
               "a staged image cannot be larger than the firmware partition");
_Static_assert(FW_STAGING_DATA_OFFSET + FW_UP_MAX_SIZE <= FW_RESOURCE_OFFSET,
               "staging area overruns the resource region (FLASH_TARGET_OFFSET)");
// ⚠️ THE wall between the staged image and the apply progress log, which lives at
// the top of the same region. Raising FW_UP_MAX_SIZE without moving the log fails
// the build here instead of silently erasing 32 KB of the source the applier is
// about to copy -- after the pre-copy CRC has already verified it, and where the
// post-copy compare would read that same corrupted source and report a match.
_Static_assert(FW_STAGING_DATA_OFFSET + FW_UP_MAX_SIZE <= FW_APPLY_LOG_OFFSET,
               "a staged image can reach the apply progress log");
_Static_assert(FW_APPLY_LOG_OFFSET + FW_APPLY_LOG_BYTES <= FW_RESOURCE_OFFSET,
               "the apply progress log runs into the resource region");

// The wear-levelling EEPROM sits at the TOP of flash, inside what the map above
// calls the resource region, so the last resource slot has to stop short of it.
// Two guards, because the reservation is expressed twice and they must agree:
_Static_assert(FW_EEPROM_RESERVE_SIZE == WEAR_LEVELING_BACKING_SIZE,
               "fw_staging.h EEPROM reservation disagrees with WEAR_LEVELING_BACKING_SIZE");
_Static_assert(FW_RESOURCE_OFFSET + FW_DOOMPACK_SLOT_OFF + FW_DOOMPACK_SLOT_SIZE
                   <= PICO_FLASH_SIZE_BYTES - WEAR_LEVELING_BACKING_SIZE,
               "DoomPack slot overlaps the EEPROM backing store at the top of flash");

// ---------------------------------------------------------------------------
// Dual-core flash safety: halt core1 via PSM reset before any flash_range_*
// call to prevent a CPU LOCKUP on core1.
//
// Background: flash_range_erase/program call flash_flush_cache() internally,
// which empties the XIP instruction cache while XIP is still in raw mode.
// Core1's tight polling loop (multicore_fifo_pop_blocking) runs from flash.
// After cache flush, core1's next instruction fetch causes a bus fault; the
// HardFault handler (also in flash) faults again → Cortex-M0+ CPU LOCKUP.
// With core1 in PSM reset it executes nothing, so the cache flush is safe.
//
// RP2040 PSM registers (datasheet §2.14):
//   Base 0x40010000; FRCE_OFF +0x04; DONE +0x0C; PROC1 = bit 16.
//
// Strategy: keep core1 halted for the *entire* slave-side update sequence
// (begin_deferred → all sector erases → all page writes → finalize).
// This avoids the ChibiOS Vector80/NMI danger window that arises each time
// core1 is restarted from ROM: between ROM writing VTOR=0x10000100 and
// cpsid i in core1_entry(), SIO_IRQ_PROC1 can fire and trigger a ChibiOS
// NMI context switch on core1 with no thread state → hang.  With 60+
// restarts per update the probability of hitting that window is high.
// On the successful path fw_staging_apply_and_reboot() hard-resets the chip,
// so core1 never needs to be restarted.  On the failure path (CRC mismatch),
// fw_staging_finalize() restarts core1 explicitly.
// ---------------------------------------------------------------------------
#ifdef USE_CORE1
#include "polymod_core1.h"

#define _PSM_WDSEL     (*(volatile uint32_t *)(0x40010000u + 0x08u))
#define _WD_CTRL       (*(volatile uint32_t *)(0x40058000u + 0x00u))
// SCRATCH0..3 are free for us; the bootrom owns SCRATCH4..7. These survive a
// watchdog reset (not a power cycle), which is what makes them the only channel a
// self-flash has: the console is gone, interrupts are off, and the routine never
// returns. A half that comes back can then say how far its copy got; a half that
// does not come back is identified by its silence.
#define _WD_SCRATCH0   (*(volatile uint32_t *)(0x40058000u + 0x0cu))
#define _WD_SCRATCH1   (*(volatile uint32_t *)(0x40058000u + 0x10u))
#define _WD_SCRATCH2   (*(volatile uint32_t *)(0x40058000u + 0x14u))
#define _WD_SCRATCH4   (*(volatile uint32_t *)(0x40058000u + 0x1cu))
#define FW_APPLY_CRUMB_SECTOR 0xB0070000u   // | sector currently being written
#define FW_APPLY_CRUMB_DONE   0xB007D01Eu   // copy loop finished, about to reset
#define FW_APPLY_DONE_MAGIC   0xD09EF1A5u   // same, recorded in flash so it outlives a power cycle
#define FW_APPLY_LOG_MAGIC    0x5EC70000u   // | sector, one page per completed sector
#define FW_APPLY_LOG_START    0x57A27EDDu   // written to the log's LAST page before sector 1 is touched
// Bracket markers around the FIRST image sector only -- the copy dies there, and log
// writes provably work at that moment, so these name the exact call that kills it.
#define FW_APPLY_LOG_PRE_ERASE   0xB1000001u
#define FW_APPLY_LOG_POST_ERASE  0xB1000002u
#define FW_APPLY_LOG_POST_PROG   0xB1000003u
#define FW_APPLY_LOG_PAGE_PRE    121u
#define FW_APPLY_LOG_POST        122u
#define FW_APPLY_LOG_PAGE_PROG   123u
#define FW_APPLY_LOG_POST_READ   0xB1000004u
#define FW_APPLY_LOG_PAGE_READ   124u
// A per-sector progress log, far enough into staging (1 MB in) to be clear of the
// image and of everything FW_UP_BEGIN erases. 8 sectors = 128 pages >= the 121 the
// image needs, so the copy can record EXACTLY how far it got in a place that outlives
// the power cycle a BOOTSEL recovery needs.
// FW_APPLY_LOG_* live in the header, beside the FW_UP_MAX_SIZE they are asserted against.
#define FW_APPLY_LOG_PAGES    (FW_APPLY_LOG_SECTORS * (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE))

// DMA is the LAST bus master that can touch XIP once core1 is in reset and interrupts
// are off. A channel still in flight whose read address is in flash will stall the bus
// the moment the QSPI leaves XIP mode for an erase -- and nothing on this path stops
// DMA. Abort every channel and wait for them to actually go quiet. Raw register writes
// (DMA_BASE + CHAN_ABORT) so this stays inlinable in the RAM routine with no call.
#define _DMA_ABORT (*(volatile uint32_t *)(0x50000000u + 0x444u))
static inline void __attribute__((always_inline)) dma_abort_all(void) {
    _DMA_ABORT = 0xFFFFu;
    __asm volatile ("dsb" ::: "memory");
    uint32_t spins = 0;
    while (_DMA_ABORT && spins < 1000000u) spins++;
}

#define _PSM_FRCE_OFF  (*(volatile uint32_t *)0x40010004u)
#define _PSM_DONE      (*(volatile uint32_t *)0x4001000cu)
#define _PSM_PROC1_BIT (1u << 16)

// Spin until the PSM has actually driven core1 into reset, and report how long it
// took. Writing FRCE_OFF and issuing a DSB only guarantees the STORE reached the
// peripheral -- it says nothing about the reset having propagated, and core1 keeps
// fetching instructions from XIP until it has. The moment core0 takes the QSPI out
// of XIP mode for an erase, an in-flight core1 fetch can never complete: the bus
// stalls, flash_range_erase() never returns, and the half is wedged mid-apply with
// no reset and no way to say so.
//
// always_inline because callers include the RAM-resident apply routine, which must
// not generate a call into flash (invariant 1 there).  Bounded so a PSM that never
// reports DONE degrades to the old behaviour instead of replacing one hang with
// another; the count is a breadcrumb, not a control flow input.
static inline uint32_t __attribute__((always_inline)) psm_wait_proc1_off(void) {
    uint32_t spins = 0;
    while ((_PSM_DONE & _PSM_PROC1_BIT) && spins < 1000000u) {
        spins++;
    }
    return spins;
}

// True while this module holds a PSM reset on core1.
static bool s_core1_halted = false;

static void fw_staging_halt_core1(void) {
    _PSM_FRCE_OFF |= _PSM_PROC1_BIT;
    __asm volatile ("dsb" ::: "memory");
    // ...and then WAIT for it. The DSB orders the store; only PSM->DONE says the
    // reset has landed and core1 has stopped fetching from XIP. See psm_wait_proc1_off().
    (void)psm_wait_proc1_off();
    s_core1_halted = true;
}

static void fw_staging_restart_core1(void) {
    _PSM_FRCE_OFF &= ~_PSM_PROC1_BIT;
    s_core1_halted = false;
    // BOUNDED relaunch, NOT the unbounded multicore_launch_core1(). core0 can
    // reach here with core1's FIFO launch handshake left desynced by a prior Doom
    // engine launch/stop cycle (doom_engine_stop uses the bounded launcher for the
    // same reason). The unbounded handshake then blocks FOREVER, freezing this
    // half — on the SLAVE that means it never answers the master's FONTPACK BEGIN
    // re-poll, so the master polls '~' forever and the flash hangs. Seen on the
    // 3rd consecutive doom-slot flash of the FW-9 rig set (accept/tampered/unsigned),
    // once two doom load+teardown cycles had degraded core1. Bounded: a still-wedged
    // core1 leaves the RLE service down until the next reboot (the identical worst
    // case doom_engine_stop already accepts) but keeps THIS half's main loop alive,
    // so the erase's cleared s_erase_pending is actually observable to the master.
    if (!multicore_launch_core1_bounded(100u * 1000u)) {
        uprintf("fw_staging: core1 relaunch timed out — RLE service down until reboot\n");
    }
}
#endif

// ---------------------------------------------------------------------------
// Linker-exported firmware boundary symbols
// ---------------------------------------------------------------------------
extern uint8_t __flash_binary_start;
extern uint8_t __flash_binary_end;

static bool     s_initialized = false;
static bool     s_fw_up_active = false;

// Target of the current begin/chunk/finalize sequence (set at begin).
static uint8_t  s_target = FW_TARGET_FIRMWARE;

// FONTPACK bundle slot within the resource region (set by fw_staging_set_fontpack_slot
// before begin). Each bundle is flashed to its own fixed sector-aligned slot, so the
// write base is FW_RESOURCE_OFFSET + slot offset and the size cap is the slot size.
static uint32_t s_fontpack_slot_off  = 0;
static uint32_t s_fontpack_slot_size = FONTPACK_FLASH_MAX_SIZE;

// FONTPACK/DOOMWAD write in place at their slot in the resource region;
// FIRMWARE stages at FW_STAGING_DATA_OFFSET behind a 4 KB header sector.
static inline uint32_t target_data_offset(void) {
    return (s_target != FW_TARGET_FIRMWARE) ? (FW_RESOURCE_OFFSET + s_fontpack_slot_off)
                                            : FW_STAGING_DATA_OFFSET;
}
// FIRMWARE prepends a header sector (erased + stamped); the resource targets
// have none (the pack / WHX carry their own header at byte 0).
static inline bool target_has_header(void) {
    return s_target == FW_TARGET_FIRMWARE;
}
static inline uint32_t target_max_size(void) {
    return (s_target != FW_TARGET_FIRMWARE) ? s_fontpack_slot_size : FW_UP_MAX_SIZE;
}

void fw_staging_set_fontpack_slot(uint32_t slot_off, uint32_t slot_size) {
    s_fontpack_slot_off  = slot_off;
    s_fontpack_slot_size = slot_size;
}

// Active FONTPACK bundle slot offset (relative to FW_RESOURCE_OFFSET) of the
// in-flight flash, so the status OLED can name the bundle being written. Only
// meaningful while fw_staging_active_target() == FW_TARGET_FONTPACK.
uint32_t fw_staging_fontpack_slot_off(void) {
    return s_fontpack_slot_off;
}

// ---------------------------------------------------------------------------
// Internal staging state
// ---------------------------------------------------------------------------
static uint32_t s_image_size;
static uint32_t s_image_crc;

// Page-accumulation buffer (FLASH_PAGE_SIZE = 256 bytes)
static uint8_t  s_page_buf[FLASH_PAGE_SIZE];
static uint32_t s_next_offset;   // total bytes accepted so far
static uint32_t s_buf_fill;      // bytes pending in s_page_buf
static uint32_t s_staged_crc;    // running CRC32 of received image bytes (for O(1) finalize)

// FW-2: detached Ed25519 signature over the staged FIRMWARE image, set by the host
// (CMD_FW_UP_SIGNATURE) before COMMIT. Verified in fw_staging_finalize() on the master.
static uint8_t  s_signature[FW_SIG_LEN];
static bool     s_signature_present;

// FW-2 physical-presence confirmation state (see the block above
// fw_staging_awaiting_confirm for the rationale). Declared here because
// fw_staging_begin, further up, resets it per flash.
static enum { CONFIRM_IDLE = 0, CONFIRM_PENDING, CONFIRM_ACCEPTED, CONFIRM_REJECTED }
                s_confirm    = CONFIRM_IDLE;
static uint32_t s_confirm_at = 0;

static bool     s_commit_pending;
static bool     s_reboot_pending;   // deferred plain reboot (QK_REBOOT slave path)
static bool     s_fontpack_reload_pending;  // slave: defer the heavy FONTPACK reload to housekeeping

static bool fw_staging_finalize_impl(bool defer_fontpack_reload);

// ---------------------------------------------------------------------------
// Deferred-erase state (used by slave handler to avoid blocking the split link)
// ---------------------------------------------------------------------------
static bool     s_erase_pending;
static uint32_t s_erase_sector_count;  // total sectors to erase (header + data)
static uint32_t s_erase_sector_next;   // next sector index not yet erased

// ---------------------------------------------------------------------------
// Diagnostic counters (read via fw_staging_get_status)
// ---------------------------------------------------------------------------
static uint16_t s_begin_handler_calls;
static uint16_t s_chunk_handler_calls;
static uint16_t s_chunk_handler_errors;
static uint16_t s_process_deferred_calls;     // every entry into process_deferred
static uint16_t s_process_deferred_advances;  // every actual sector erase
static uint32_t s_last_chunk_offset;
static uint8_t  s_last_chunk_ack;
static uint8_t  s_last_commit_ack;   // ack the slave's COMMIT handler last returned

// ---------------------------------------------------------------------------
// Helper: chain CRC32 over a large buffer in 60 000-byte chunks
// (crc32_1byte length parameter is uint16_t, max 65535)
// ---------------------------------------------------------------------------
static uint32_t crc32_large(const uint8_t *data, uint32_t size) {
    uint32_t crc = 0;
    while (size > 0) {
        uint16_t chunk = (size > 60000u) ? 60000u : (uint16_t)size;
        crc  = crc32_1byte(data, chunk, crc);
        data += chunk;
        size -= chunk;
    }
    return crc;
}

// ---------------------------------------------------------------------------
// flash_range_program wrapped in the IRQ-disable + (conditional) core1-halt guard
// the bootrom flash ops require.  s_core1_halted lets a caller that has already
// halted core1 (e.g. inside a wider erase) reuse it without a redundant restart.
// ---------------------------------------------------------------------------
static void flash_program_guarded(uint32_t flash_offs, const uint8_t *buf, uint32_t size) {
#ifdef USE_CORE1
    bool already_halted = s_core1_halted;
    if (!already_halted) fw_staging_halt_core1();
#endif
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(flash_offs, buf, size);
    restore_interrupts(irq);
#ifdef USE_CORE1
    if (!already_halted) fw_staging_restart_core1();
#endif
}

// ---------------------------------------------------------------------------
// Flush accumulated page buffer to staging flash
// ---------------------------------------------------------------------------
static void flush_page(void) {
    uint32_t flash_offs = target_data_offset() + (s_next_offset - s_buf_fill);
    flash_program_guarded(flash_offs, s_page_buf, FLASH_PAGE_SIZE);
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);
    s_buf_fill = 0;
}

// ---------------------------------------------------------------------------
// Write the staging header sector {magic, size, crc}.  Called by finalize on a
// CRC match so the staged image is self-describing — fw_staging_apply_and_reboot()
// validates the magic and reads the size from here.  The header sector was erased
// in fw_staging_begin*(), so this single program is valid.
// ---------------------------------------------------------------------------
static void write_staging_header(uint32_t size, uint32_t crc) {
    uint8_t  hdr[FLASH_PAGE_SIZE];
    uint32_t words[3] = { FW_STAGING_MAGIC, size, crc };
    memset(hdr, 0xFF, sizeof(hdr));
    memcpy(hdr, words, sizeof(words));
    flash_program_guarded(FW_STAGING_OFFSET, hdr, FLASH_PAGE_SIZE);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Captured once at boot from the watchdog scratch registers, then cleared so a later
// boot cannot re-report a stale apply. Printed by the boot banner (boot_diag.c).
static uint32_t s_crumb_sector = 0;
static uint32_t s_crumb_done   = 0;
static uint32_t s_crumb_spins  = 0;
static uint32_t s_done_sectors = 0;   // from the in-flash record, 0 = none present
static uint32_t s_done_size    = 0;
static uint32_t s_done_spins   = 0;
static uint32_t s_done_diff_at = 0xFFFFFFFFu;   // offset of the first byte the copy got wrong
static uint32_t s_done_diff_got = 0;
static uint32_t s_done_diff_want = 0;
static uint32_t s_log_max   = 0;   // highest sector the copy logged as completed
static uint32_t s_log_min   = 0;   // lowest ditto -- the copy runs top-down, so this is where it stopped
static uint32_t s_log_count = 0;   // how many sectors it logged at all
static bool     s_log_started = false;   // the copy reached its first sector
static uint32_t s_mark_stage = 0;        // 0 none, 1 pre-erase, 2 post-erase, 3 post-program
static uint32_t s_mark_addr  = 0;        // flash offset of the first image sector touched
static uint32_t s_mark_read_erased = 0;  // its first word read back after the erase
static uint32_t s_mark_read_progd  = 0;  // ...and after the first page was programmed
static uint32_t s_mark_src_word    = 0;  // first word read back from the staging source

bool fw_staging_apply_marks(uint32_t *stage, uint32_t *addr, uint32_t *erased, uint32_t *progd, uint32_t *srcw) {
    if (s_mark_stage == 0) return false;
    if (stage)  *stage  = s_mark_stage;
    if (addr)   *addr   = s_mark_addr;
    if (erased) *erased = s_mark_read_erased;
    if (progd)  *progd  = s_mark_read_progd;
    if (srcw)   *srcw   = s_mark_src_word;
    return true;
}

bool fw_staging_apply_progress(uint32_t *lowest, uint32_t *highest, uint32_t *count) {
    if (s_log_count == 0) return false;
    if (lowest)  *lowest  = s_log_min;
    if (highest) *highest = s_log_max;
    if (count)   *count   = s_log_count;
    return true;
}

bool fw_staging_apply_started(void) { return s_log_started; }

bool fw_staging_last_apply_completed(uint32_t *sectors, uint32_t *size, uint32_t *psm_spins) {
    if (s_done_sectors == 0) return false;
    if (sectors)   *sectors   = s_done_sectors;
    if (size)      *size      = s_done_size;
    if (psm_spins) *psm_spins = s_done_spins;
    return true;
}

bool fw_staging_last_apply_diff(uint32_t *offset, uint32_t *got, uint32_t *want) {
    if (s_done_sectors == 0) return false;
    if (offset) *offset = s_done_diff_at;
    if (got)    *got    = s_done_diff_got;
    if (want)   *want   = s_done_diff_want;
    return true;
}

bool fw_staging_apply_breadcrumb(uint32_t *last_sector, bool *completed, uint32_t *psm_spins) {
    if ((s_crumb_sector & 0xFFFF0000u) != FW_APPLY_CRUMB_SECTOR) return false;
    if (last_sector) *last_sector = s_crumb_sector & 0xFFFFu;
    if (completed)   *completed   = (s_crumb_done == FW_APPLY_CRUMB_DONE);
    if (psm_spins)   *psm_spins   = s_crumb_spins;
    return true;
}

// Public core1 lockout for a caller that is about to do its OWN flash work.
//
// The RP2040 hazard is not specific to this file: while core0 has the QSPI out of
// XIP mode, core1 must not fetch an instruction from flash or the bus stalls and the
// operation never completes. fw_staging halts core1 around every write it makes --
// but QMK's wear-levelling backing store (platforms/chibios/drivers/wear_leveling/
// wear_leveling_rp2040_flash.c) calls flash_range_erase() with core1 running and free
// to be anywhere. Normally the window is small enough to get away with; on the apply
// path save_all_dirty() can force a consolidation at the exact moment core1 has just
// been relaunched after the staging erase, which is where it stops being survivable.
//
// Wrapping the whole flush is the fix, and it is a lockout rather than a fw_staging
// internal because the caller (poly_keymap's apply block) owns the sequencing.
void fw_staging_core1_lockout_begin(void) {
#ifdef USE_CORE1
    if (!s_core1_halted) fw_staging_halt_core1();
#endif
}

void fw_staging_core1_lockout_end(void) {
#ifdef USE_CORE1
    if (s_core1_halted) fw_staging_restart_core1();
#endif
}

void fw_staging_init(void) {
    for (uint32_t i = 0; i < FW_APPLY_LOG_PAGES; i++) {
        const uint32_t w = *(const uint32_t *)(XIP_BASE + FW_APPLY_LOG_OFFSET + i * FLASH_PAGE_SIZE);
        const uint32_t w1 = *(const uint32_t *)(XIP_BASE + FW_APPLY_LOG_OFFSET + i * FLASH_PAGE_SIZE + 4);
        if (w == FW_APPLY_LOG_START) {
            s_log_started = true;
        } else if (w == FW_APPLY_LOG_PRE_ERASE) {
            s_mark_stage = 1; s_mark_addr = w1;
        } else if (w == FW_APPLY_LOG_POST_ERASE) {
            if (s_mark_stage < 2) s_mark_stage = 2;
            s_mark_read_erased = w1;
        } else if (w == FW_APPLY_LOG_POST_READ) {
            if (s_mark_stage < 3) s_mark_stage = 3;
            s_mark_src_word = w1;
        } else if (w == FW_APPLY_LOG_POST_PROG) {
            if (s_mark_stage < 4) s_mark_stage = 4;
            s_mark_read_progd = w1;
        } else if ((w & 0xFFFF0000u) == FW_APPLY_LOG_MAGIC) {
            s_log_count++;
            const uint32_t sec = w & 0xFFFFu;
            if (sec > s_log_max || s_log_count == 1) s_log_max = sec;
            if (sec < s_log_min || s_log_count == 1) s_log_min = sec;
        }
    }
    const uint32_t *done = (const uint32_t *)(XIP_BASE + FW_STAGING_OFFSET + FLASH_PAGE_SIZE);
    if (done[0] == FW_APPLY_DONE_MAGIC) {
        s_done_sectors = done[1];
        s_done_size    = done[2];
        s_done_spins   = done[3];
        s_done_diff_at   = done[4];
        s_done_diff_got  = done[5];
        s_done_diff_want = done[6];
    }
    s_crumb_sector = _WD_SCRATCH0;
    s_crumb_done   = _WD_SCRATCH1;
    s_crumb_spins  = _WD_SCRATCH2;
    _WD_SCRATCH0 = 0;
    _WD_SCRATCH1 = 0;
    _WD_SCRATCH2 = 0;

    s_initialized          = true;
    s_commit_pending       = false;
    s_reboot_pending       = false;
    s_erase_pending        = false;
    s_fw_up_active         = false;
    // Diagnostic counters: leave the cumulative call counts in place across
    // init so a re-arm during the same power cycle is visible, but clear the
    // last-chunk fields so they only ever describe the most recent attempt.
    s_last_chunk_offset    = 0;
    s_last_chunk_ack       = 0;
    s_last_commit_ack      = 0;
#ifdef USE_CORE1
    s_core1_halted   = false;
#endif
}

// Synchronous begin: erases staging sector-by-sector with interrupts briefly
// re-enabled between sectors so USB and watchdog stay alive.  Use this on the
// master (USB side); the host app timeout must cover the full erase duration
// (~50 ms × ceil(image_size/4096) sectors).
void fw_staging_begin(uint32_t image_size, uint32_t image_crc) {
    fw_staging_begin_target(image_size, image_crc, FW_TARGET_FIRMWARE);
}

void fw_staging_begin_target(uint32_t image_size, uint32_t image_crc, uint8_t target) {
    if (!s_initialized) fw_staging_init();

    s_target         = target;
    s_next_offset    = 0;
    s_buf_fill       = 0;
    s_staged_crc     = 0;
    s_signature_present = false;  // FW-2: each new image must re-supply its signature
    s_confirm           = CONFIRM_IDLE;   // ...and re-confirm, if it turns out unsigned
    s_commit_pending = false;
    s_erase_pending  = false;
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);

    if (image_size == 0 || image_size > target_max_size()) {
        s_image_size = 0;
        s_image_crc  = 0;
        return;
    }
    s_image_size = image_size;
    s_image_crc  = image_crc;
    s_fw_up_active = true;
    // Per ATTEMPT, not per boot: fw_staging_init() runs once at keyboard_post_init,
    // so clearing the verdict only there let a second flash in the same power cycle
    // inherit the first one's. That matters because the master now READS this to
    // classify a lost COMMIT ack — a stale refusal would turn a genuine link failure
    // into a reported data rejection and cost a full re-stream instead of a free
    // COMMIT retry, which is the exact case the probe exists to get right.
    s_last_commit_ack = 0;

    // Erase the header sector (FIRMWARE only) then each data sector individually.
    // Re-enabling interrupts between sectors keeps USB/watchdog responsive
    // during the ~50 ms erase window per sector.
    uint32_t data_sectors = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uint32_t irq;

#ifdef USE_CORE1
    fw_staging_halt_core1();
#endif
    if (target_has_header()) {
        irq = save_and_disable_interrupts();
        flash_range_erase(FW_STAGING_OFFSET, FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
    }

    for (uint32_t s = 0; s < data_sectors; s++) {
        irq = save_and_disable_interrupts();
        flash_range_erase(target_data_offset() + s * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
    }
#ifdef USE_CORE1
    fw_staging_restart_core1();
#endif
}

// Deferred begin: stores parameters, schedules sector-by-sector erase via
// fw_staging_process_deferred() called from housekeeping_task_user().
// Returns immediately — safe to call from a split-link transaction handler.
void fw_staging_begin_deferred(uint32_t image_size, uint32_t image_crc) {
    fw_staging_begin_deferred_target(image_size, image_crc, FW_TARGET_FIRMWARE);
}

void fw_staging_begin_deferred_target(uint32_t image_size, uint32_t image_crc, uint8_t target) {
    if (!s_initialized) fw_staging_init();

    s_target         = target;
    s_next_offset    = 0;
    s_buf_fill       = 0;
    s_staged_crc     = 0;
    s_signature_present = false;  // FW-2: each new image must re-supply its signature
    s_confirm           = CONFIRM_IDLE;   // ...and re-confirm, if it turns out unsigned
    s_commit_pending = false;
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);

    if (image_size == 0 || image_size > target_max_size()) {
        s_image_size    = 0;
        s_image_crc     = 0;
        s_erase_pending = false;
        return;
    }
    s_image_size = image_size;
    s_image_crc  = image_crc;
    s_fw_up_active = true;
    // Per ATTEMPT, not per boot: fw_staging_init() runs once at keyboard_post_init,
    // so clearing the verdict only there let a second flash in the same power cycle
    // inherit the first one's. That matters because the master now READS this to
    // classify a lost COMMIT ack — a stale refusal would turn a genuine link failure
    // into a reported data rejection and cost a full re-stream instead of a free
    // COMMIT retry, which is the exact case the probe exists to get right.
    s_last_commit_ack = 0;

    uint32_t data_sectors   = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    // FIRMWARE: index 0 = header sector, 1..N = data. FONTPACK: 0..N-1 = data (no header).
    s_erase_sector_count    = (target_has_header() ? 1u : 0u) + data_sectors;
    s_erase_sector_next     = 0;
    s_erase_pending         = true;
#ifdef USE_CORE1
    // Halt core1 now and keep it halted for the entire update sequence
    // (all sector erases + all page writes + finalize).  Restarting core1
    // once per sector erase exposes the ChibiOS Vector80/NMI danger window
    // on every restart; keeping it halted throughout eliminates that risk.
    // On the success path the chip hard-resets, so core1 is never restarted.
    // On the failure path fw_staging_finalize() restarts core1 explicitly.
    fw_staging_halt_core1();
#endif
}

// Erase one staging sector per call.  Must be called repeatedly from
// housekeeping_task_user() until fw_staging_erase_pending() returns false.
void fw_staging_process_deferred(void) {
    // Bump the call counter unconditionally — this is what proves the slave's
    // housekeeping is actually invoking us (vs. silently never being scheduled,
    // which is what the symptom would look like from the master side).
    if (s_process_deferred_calls != UINT16_MAX) s_process_deferred_calls++;

    if (!s_erase_pending) return;

    if (s_erase_sector_next >= s_erase_sector_count) {
        s_erase_pending = false;
        return;
    }

    // Rate-limit to one sector per 70ms: each sector takes ~50ms (irqs disabled),
    // leaving ~20ms of UART-responsive time between erasures.  Without this gate
    // housekeeping fires every ~1ms and the slave spends >98% of the erase period
    // with interrupts off, making the split link completely unresponsive and
    // causing every chunk transaction from the master to fail.
    static uint32_t s_last_sector_ms = 0;
    if (timer_elapsed32(s_last_sector_ms) < 70) return;

    uint32_t offset;
    if (target_has_header()) {
        // FIRMWARE: sector 0 = header, sectors 1..N = data.
        offset = (s_erase_sector_next == 0)
                     ? FW_STAGING_OFFSET
                     : target_data_offset() + (s_erase_sector_next - 1) * FLASH_SECTOR_SIZE;
    } else {
        // FONTPACK: no header — sectors 0..N-1 are data.
        offset = target_data_offset() + s_erase_sector_next * FLASH_SECTOR_SIZE;
    }
    // core1 is already halted by fw_staging_begin_deferred(); no halt/restart here.
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
    s_last_sector_ms = timer_read32();

    // Bump the advance counter — proves we actually got back from
    // flash_range_erase (i.e. the bootrom returned, didn't hang in XIP-off).
    if (s_process_deferred_advances != UINT16_MAX) s_process_deferred_advances++;

    s_erase_sector_next++;
    if (s_erase_sector_next >= s_erase_sector_count) {
        s_erase_pending = false;
#ifdef USE_CORE1
        // DIAGNOSTIC PROBE (2026-05-29): restart core1 the instant erase
        // completes, before the first chunk arrives.  Hypothesis: holding
        // core1 in PSM reset across the begin->chunk handoff hard-locks the
        // slave on chunk 0 (core0 blocks forever on a full SIO FIFO signalling
        // a non-draining core1 — chunk 0 does no flash, so a flash fault can't
        // explain it).  If chunk 0 now ACKs and the lock instead moves to the
        // first page flush (the chunk at offset 224), the per-flush halt/restart
        // cycle is the culprit -> cooperative core1 park.  See CLAUDE.md fw_up.
        if (s_core1_halted) fw_staging_restart_core1();
        uprintf("fw_staging_process_deferred: erase complete (%lu sectors), core1 restarted\n", s_erase_sector_count);
#else
        uprintf("fw_staging_process_deferred: erase complete (%lu sectors)\n", s_erase_sector_count);
#endif
    } else {
#ifdef FW_UP_VERBOSE
        uprintf("fw_staging_process_deferred: erased sector %lu/%lu\n", s_erase_sector_next, s_erase_sector_count);
#endif
    }
}

bool fw_staging_write_chunk(uint32_t offset, const uint8_t *data, uint8_t len) {
    if (!s_initialized) {
        uprintf("fw_staging_write_chunk: not initialized\n");
        return false;
    }
    if (s_erase_pending) {
        uprintf("fw_staging_write_chunk: erase still pending (sector %lu/%lu)\n", s_erase_sector_next, s_erase_sector_count);
        return false;
    }
    if (offset < s_next_offset) {
        // Duplicate of an already-staged chunk: the host retries a chunk when the
        // ACK got lost on the way back (slave UART reply corrupted, or the USB
        // reply to the host timed out).  The data is already in staging and the
        // running CRC already includes it — ACK again without rewriting, so the
        // retry protocol stays idempotent (NOR flash cannot rewrite anyway).
        uprintf("fw_staging_write_chunk: duplicate offset=%lu (next=%lu) — ack\n", offset, s_next_offset);
        return true;
    }
    if (offset != s_next_offset) {
        uprintf("fw_staging_write_chunk: offset mismatch got=%lu expected=%lu\n", offset, s_next_offset);
        return false;
    }
    if (offset >= s_image_size) {
        uprintf("fw_staging_write_chunk: offset=%lu >= image_size=%lu\n", offset, s_image_size);
        return false;
    }

    // Clamp trailing bytes for the partial last chunk — callers always pass
    // FW_UP_CHUNK_SIZE so the pad bytes (0xFF) past s_image_size must be dropped.
    if (offset + len > s_image_size) {
        len = (uint8_t)(s_image_size - offset);
    }

    // Accumulate a running CRC over the image bytes as they arrive so
    // fw_staging_finalize() can verify in O(1) instead of re-scanning 244 KB of
    // staged flash — that scan overflowed the split-transaction window and made
    // the master mis-report COMMIT as "CRC mismatch" (see FW_UP_BASELINE.md run 6).
    s_staged_crc = crc32_1byte(data, len, s_staged_crc);

    const uint8_t *src       = data;
    uint8_t        remaining = len;

    while (remaining > 0) {
        // NB: space/copy MUST be wider than uint8_t — FLASH_PAGE_SIZE is 256, so
        // (FLASH_PAGE_SIZE - s_buf_fill) is 256 when the page buffer is empty and
        // truncates to 0 in a uint8_t, making copy=0 and spinning this loop
        // forever (the chunk-0 slave hard-lock, found 2026-05-30).
        uint32_t space = FLASH_PAGE_SIZE - s_buf_fill;
        uint32_t copy  = (remaining < space) ? remaining : space;

        memcpy(s_page_buf + s_buf_fill, src, copy);
        s_buf_fill    += copy;
        s_next_offset += copy;
        src           += copy;
        remaining     -= copy;

        if (s_buf_fill == FLASH_PAGE_SIZE) {
            flush_page();
        }
    }
    return true;
}

void fw_staging_set_signature(const uint8_t sig[FW_SIG_LEN]) {
    memcpy(s_signature, sig, FW_SIG_LEN);
    s_signature_present = true;
}

// FW-2 physical-presence confirmation. Under FW_REQUIRE_SIGNATURE an unsigned or
// badly signed image is not simply refused — a developer flashing their own build
// needs a way through that malware cannot take. An acknowledgement carried over
// HID would be worthless: the HID flash surface is exactly what signing defends,
// so anything a host can send, an attacker can send too. A KEYPRESS cannot be
// forged remotely, so the answer is a physical act on the keyboard.
//
// The state is RAM-only and re-armed per flash (fw_staging_begin clears it), so it
// cannot survive a reboot or linger as a permanently disabled check.
// (s_confirm / s_confirm_at are declared up with s_signature — fw_staging_begin
// needs to reset them and sits above this block.)

// Why the last COMMIT was refused. Without this the caller cannot tell a
// signature refusal from a CRC mismatch — fw_staging_finalize() returns a bare
// bool and the signature check sits behind the CRC result, so both surfaced as
// the same NACK. The rig duly reported "staged CRC mismatch" for an image whose
// CRC was perfect, and sent a diagnosis the wrong way (2026-08-05).
static bool s_refused_unsigned = false;

bool fw_staging_awaiting_confirm(void) {
    return s_confirm == CONFIRM_PENDING;
}

bool fw_staging_confirm_in_progress(void) {
    return s_confirm != CONFIRM_IDLE;
}

void fw_staging_confirm_answer(bool accept) {
    if (s_confirm != CONFIRM_PENDING) return;   // first answer wins; ignore stray keys
    s_confirm = accept ? CONFIRM_ACCEPTED : CONFIRM_REJECTED;
    uprintf("FW_UP: unsigned image %s on the keyboard\n", accept ? "ACCEPTED" : "REJECTED");
}

void fw_staging_confirm_tick(void) {
    // timer_elapsed32() is modular, so this stays correct across the 49.7-day
    // timer wrap — the same trap that once disabled idle for a 25-day window
    // (see the update.c signed-timestamp bug).
    if (s_confirm == CONFIRM_PENDING && timer_elapsed32(s_confirm_at) >= FW_CONFIRM_WINDOW_MS) {
        s_confirm = CONFIRM_REJECTED;
        uprint("FW_UP: unsigned-image confirmation timed out — refusing\n");
    }
}

bool fw_staging_refused_unsigned(void) {
    return s_refused_unsigned;
}

// Is a real signing key compiled in, or is this still the all-zero placeholder that
// fw_pubkey.h ships before `gen_signing_key.py` has been run?
//
// ⚠️ The all-zero key is NOT an inert value that simply fails every check. Its
// encoding is y=0, which decodes to a point that is genuinely ON the curve with
// ORDER 4 — and crypto_eddsa_check_equation() only verifies that A and R are on the
// curve and that 0 <= S < L. It has no low-order-key rejection. With an order-4 A,
// [h]A depends only on h mod 4, so an attacker can guess that value and land a
// forgery in a handful of attempts. A placeholder key therefore makes enforcement
// FORGEABLE rather than fail-closed — the opposite of the intuition that a dummy key
// "rejects everything". Guard it explicitly so a botched key regeneration or a bad
// merge can never silently downgrade FW_REQUIRE_SIGNATURE into theatre.
static bool fw_pubkey_provisioned(void) {
    uint8_t acc = 0;
    for (size_t i = 0; i < sizeof(FW_SIGNING_PUBKEY); i++) {
        acc |= FW_SIGNING_PUBKEY[i];
    }
    return acc != 0;
}

// FW-2: verify the staged FIRMWARE image's Ed25519 signature against the embedded
// public key. The image is read straight from XIP flash (memory-mapped), so no RAM
// copy of the (up to ~2 MB) image is needed. Returns:
//    1 = valid signature, -1 = present but invalid, 0 = no signature supplied.
// HEAVY (SHA-512 over the whole image, ~0.4 s) — master only; never inside a split
// transaction window.
static int fw_staging_check_signature(void) {
    if (!s_signature_present) return 0;
    if (!fw_pubkey_provisioned()) {
        // Report INVALID, not UNSIGNED: a signature WAS supplied, we just have no
        // trustworthy key to judge it with. Under enforcement both are refused.
        uprintf("FW_UP: no signing key provisioned (placeholder pubkey) — cannot verify\n");
        return -1;
    }
    const uint8_t *img = (const uint8_t *)(XIP_BASE + FW_STAGING_DATA_OFFSET);
    // monocypher crypto_ed25519_check() returns 0 on success, -1 on any failure.
    return (crypto_ed25519_check(s_signature, FW_SIGNING_PUBKEY, img, s_image_size) == 0) ? 1 : -1;
}

bool fw_staging_finalize(void) {
    return fw_staging_finalize_impl(false);
}

// Slave-side finalize. Identical to fw_staging_finalize() EXCEPT the heavy
// FONTPACK reload (a full-body CRC32 + reassemble over the whole ~459 KB pack,
// ~50 ms) is DEFERRED to housekeeping instead of run inline. The slave runs
// finalize inside the USER_SYNC_FLASH_STAGE (COMMIT op) split-transaction callback, whose
// receive window is ~20 ms — the inline reload overran it, so the master timed
// out and mis-reported COMMIT as a CRC failure even though the pack loaded fine
// (same class of bug as the master-side re-scan, run 6). The O(1) transport CRC
// here already proves our staged bytes are identical to the master's (which
// independently passed the full pack CRC + load), so we ACK on that and let
// fw_staging_process_fontpack_reload() do the reload off the transaction path.
bool fw_staging_finalize_defer_reload(void) {
    return fw_staging_finalize_impl(true);
}

// Drain the deferred FONTPACK reload from housekeeping_task_user(). Returns true
// iff it actually reloaded this call (so the caller can request a display refresh).
bool fw_staging_process_fontpack_reload(void) {
    if (!s_fontpack_reload_pending) return false;
    s_fontpack_reload_pending = false;
    fontpack_reload();
    return true;
}

static bool fw_staging_finalize_impl(bool defer_fontpack_reload) {
    // Cleared per attempt: a stale reason would mislabel a later CRC failure as
    // a signature refusal, which is the exact confusion this flag exists to end.
    s_refused_unsigned = false;
    if (!s_initialized) return false;

    // Flush any partial final page (padded with 0xFF already from fw_staging_begin/flush_page)
    if (s_buf_fill > 0) {
        flush_page();
    }

    // Verify using the running CRC accumulated in fw_staging_write_chunk (O(1)).
    // (Re-scanning the 244 KB staged region here overflowed the split-transaction
    // window and made the master mis-report COMMIT as "CRC mismatch" — run 6.)
    bool ok = (s_staged_crc == s_image_crc);

    if (ok && s_target == FW_TARGET_DOOMWAD) {
        // DOOMWAD: the WHX is fully written in place; validate its magic — O(1),
        // safe inline on both halves (nothing reloads; the doom engine maps it
        // at TINY_WAD_ADDR only when game mode boots).
        const uint8_t *whx = (const uint8_t *)(XIP_BASE + FW_RESOURCE_OFFSET + s_fontpack_slot_off);
        ok = whx[0] == 'I' && whx[1] == 'W' && whx[2] == 'H' && whx[3] == 'X';
    } else if (ok && s_target == FW_TARGET_DOOMPACK) {
        // DOOMPACK: the executable engine pack is fully written in place.
        // O(1) header sanity only — magic + size (the transport CRC above
        // already proves byte-identity with the host's image; the full
        // image-CRC + ram_base pairing checks are the loader's job at game
        // entry, doom/PACK_DESIGN.md §2). Keeping this O(1) matters on the
        // slave: finalize runs inside the split-transaction window.
        const uint8_t  *p   = (const uint8_t *)(XIP_BASE + FW_RESOURCE_OFFSET + s_fontpack_slot_off);
        const uint32_t *hdr = (const uint32_t *)(const void *)p;
        ok = p[0] == 'P' && p[1] == 'l' && p[2] == 'y' && p[3] == 'X' &&
             hdr[2] <= s_fontpack_slot_size - 64u; // image_size fits the slot (64 = DOOM_PACK_HDR_SIZE)
    } else if (ok && !target_has_header()) {
        // FONTPACK: the pack is now fully written in place. Re-load it from XIP —
        // this independently re-validates the pack's own header CRC32 and rebuilds
        // g_all_fonts. No reboot: fonts render immediately. ok also requires the
        // pack to pass its own integrity check, not just the transport CRC.
        if (defer_fontpack_reload) {
            // Slave: defer the ~50 ms full-body verify+reassemble out of the split
            // transaction (see fw_staging_finalize_defer_reload). ok stays = the
            // O(1) transport CRC, which already guarantees byte-identity with the
            // master's verified pack, so the deferred reload cannot fail here.
            s_fontpack_reload_pending = true;
        } else {
            fontpack_reload();
            // Gate on the JUST-FLASHED slot loading as a valid PlyF (an empty wipe
            // sentinel counts), not the whole-pack fontpack_present() — which is
            // false after a full wipe and would falsely fail the last bundle.
            ok = fontpack_slot_present(s_fontpack_slot_off);
        }
    } else if (ok) {
        // FW-2: verify the image signature on the MASTER before stamping the header.
        // The heavy SHA-512 must not run inside the slave's ~20 ms split-transaction
        // window, so the slave skips it — its staged bytes are CRC-identical to the
        // master's verified image, and the master is what gates the apply.
        if (is_keyboard_master()) {
            int sig = fw_staging_check_signature();
            if (sig == 1) {
                uprintf("FW_UP: image signature OK\n");
            } else if (sig == -1) {
                uprintf("FW_UP: image signature INVALID\n");
            } else {
                uprintf("FW_UP: image UNSIGNED (no signature supplied)\n");
            }
#ifdef FW_REQUIRE_SIGNATURE
            // Enforcement: anything but a valid signature needs the user to confirm
            // ON THE KEYBOARD. The first COMMIT raises the prompt and answers '?';
            // the host re-polls COMMIT (staging state is untouched, so re-running
            // finalize is free) until a keypress or the timeout resolves it.
            //
            // We must NOT block here waiting for the answer: COMMIT runs inside
            // raw_hid_receive() on the main loop, and the matrix is scanned by that
            // same loop — a busy-wait would guarantee the keypress is never seen.
            //
            // ⚠️ The prompt is offered for an image with NO signature (sig == 0) —
            // a developer build — and NOT for one whose signature is present and
            // fails to verify (sig == -1). Those are different events: the first is
            // "you compiled this yourself", the second is "this file is not what it
            // claims to be", i.e. corruption or tampering. Inviting a keypress to
            // accept the second would hand the attacker the one thing the physical
            // gate exists to withhold — a user who has been told to press A. So an
            // invalid signature is refused outright, with no way through.
            if (sig == -1) {
                s_refused_unsigned = true;
                uprint("FW_UP: REJECTED — signature does not verify (corrupt or tampered image). "
                       "No confirmation is offered for this case.\n");
                ok = false;
                s_confirm = CONFIRM_IDLE;
            } else if (sig == 0) {
                switch (s_confirm) {
                    case CONFIRM_IDLE:
                        s_confirm    = CONFIRM_PENDING;
                        s_confirm_at = timer_read32();
                        uprintf("FW_UP: image UNSIGNED — confirm on the keyboard "
                                "(A = accept, R = reject) within %lus\n",
                                (unsigned long)(FW_CONFIRM_WINDOW_MS / 1000));
                        ok = false;
                        break;
                    case CONFIRM_PENDING:
                        ok = false;                 // still waiting; COMMIT answers '?'
                        break;
                    case CONFIRM_ACCEPTED:
                        // Consumed here, so one confirmation authorises exactly this
                        // image — a second unsigned flash prompts again.
                        s_confirm = CONFIRM_IDLE;
                        uprint("FW_UP: unsigned image ALLOWED by physical confirmation\n");
                        break;
                    case CONFIRM_REJECTED:
                        s_confirm          = CONFIRM_IDLE;
                        s_refused_unsigned = true;
                        uprint("FW_UP: REJECTED — unsigned image, not confirmed\n");
                        ok = false;
                        break;
                }
            } else if (s_confirm != CONFIRM_IDLE) {
                s_confirm = CONFIRM_IDLE;           // signed image: nothing left to ask
            }
#endif
        }
        // FIRMWARE: stamp the staging header {magic,size,crc} so the staged image
        // is self-describing and fw_staging_apply_and_reboot() can validate + size it.
        if (ok) {
            write_staging_header(s_image_size, s_image_crc);
        }
    }

    // DECOUPLED (2026-05-30, run 6): COMMIT verifies + reports success; it does NOT
    // auto-apply/reboot.  s_commit_pending stays clear — the apply is armed only by
    // the explicit FW_UP_APPLY command (fw_staging_arm_apply), phase 2 master-only.
    s_commit_pending = false;
    s_fw_up_active   = false;
#ifdef USE_CORE1
    // We are NOT rebooting here, so core1 — halted in fw_staging_begin_deferred and
    // kept halted through every chunk flush — must be restarted, or the half resumes
    // normal operation with a dead core1 (no RLE overlay decompression).
    if (s_core1_halted) fw_staging_restart_core1();
#endif
    return ok;
}

bool fw_staging_erase_pending(void) {
    return s_erase_pending;
}

bool fw_staging_fw_up_active(void) {
    return s_fw_up_active;
}

// 0xFF when idle, else the FW_TARGET_* of the in-progress flash (for a UI label).
uint8_t fw_staging_active_target(void) {
    return s_fw_up_active ? s_target : 0xFFu;
}

// Total bytes of the image currently being staged (for a progress bar). 0 if idle.
uint32_t fw_staging_image_size(void) {
    return s_fw_up_active ? s_image_size : 0u;
}

uint32_t fw_staging_next_offset(void) {
    return s_next_offset;
}

bool fw_staging_written(void) {
    return s_next_offset > 0;
}

bool fw_staging_commit_pending(void) {
    return s_commit_pending;
}

bool fw_staging_has_valid_staged_image(void) {
    const uint32_t *hdr = (const uint32_t *)(XIP_BASE + FW_STAGING_OFFSET);
    return (hdr[0] == FW_STAGING_MAGIC) && (hdr[1] > 0) && (hdr[1] <= FW_UP_MAX_SIZE);
}

void fw_staging_arm_apply(void) {
    s_commit_pending = true;
}

void fw_staging_arm_reboot(void) {
    s_reboot_pending = true;
}

bool fw_staging_reboot_pending(void) {
    return s_reboot_pending;
}

uint32_t fw_staging_get_own_fw_size(void) {
    return (uint32_t)(&__flash_binary_end - &__flash_binary_start);
}

uint32_t fw_staging_get_own_fw_crc(void) {
    return crc32_large(&__flash_binary_start, fw_staging_get_own_fw_size());
}

const uint8_t *fw_staging_get_fw_base(void) {
    return &__flash_binary_start;
}

void fw_staging_get_status(fw_staging_status_t *out) {
    if (!out) return;
    out->initialized          = s_initialized ? 1 : 0;
    out->fw_up_active         = s_fw_up_active ? 1 : 0;
    out->erase_pending        = s_erase_pending ? 1 : 0;
    out->last_chunk_ack       = s_last_chunk_ack;
    out->last_commit_ack      = s_last_commit_ack;
    out->erase_sector_next    = (uint16_t)s_erase_sector_next;
    out->erase_sector_count   = (uint16_t)s_erase_sector_count;
    out->next_offset          = s_next_offset;
    out->last_chunk_offset    = s_last_chunk_offset;
    out->begin_handler_calls  = s_begin_handler_calls;
    out->chunk_handler_calls  = s_chunk_handler_calls;
    out->chunk_handler_errors      = s_chunk_handler_errors;
    out->process_deferred_calls    = s_process_deferred_calls;
    out->process_deferred_advances = s_process_deferred_advances;
    out->pad                       = 0;
}

void fw_staging_note_begin_call(void) {
    if (s_begin_handler_calls != UINT16_MAX) s_begin_handler_calls++;
}

void fw_staging_note_chunk_call(uint32_t offset, uint8_t ack) {
    if (s_chunk_handler_calls != UINT16_MAX) s_chunk_handler_calls++;
    s_last_chunk_offset = offset;
    s_last_chunk_ack    = ack;
    // Treat anything other than SYNC_ACK as an "error" from the chunk path,
    // so a stuck slave that always returns CRC_ERR shows up clearly.
    if (ack != 0xCA /* SYNC_ACK */) {
        if (s_chunk_handler_errors != UINT16_MAX) s_chunk_handler_errors++;
    }
}

void fw_staging_note_commit_ack(uint8_t ack) {
    s_last_commit_ack = ack;
}

void fw_staging_set_fw_up_active(bool active) {
    if (!s_initialized) fw_staging_init();
    s_fw_up_active = active;
}

// ---------------------------------------------------------------------------
// RAM-resident word copy.  We must NOT call the toolchain memcpy here: it lives
// in flash (sector 1, 0x10b8) and the apply loop erases that sector — the next
// flash-resident memcpy() call would then execute erased flash and HardFault.
// This inline word loop compiles into the RAM function itself (no veneer, no
// flash fetch).  src/dst are word-aligned (page buffer + 256 B pages).
// ---------------------------------------------------------------------------
static inline void __attribute__((always_inline))
ram_word_copy(uint32_t *dst, const uint32_t *src, uint32_t nbytes) {
    for (uint32_t i = 0; i < nbytes / 4; i++) dst[i] = src[i];
}

// ---------------------------------------------------------------------------
// RAM-resident apply routine: copy staging → active firmware + hard-reset.
// Must run from RAM because it erases the flash region it was loaded from.
//
// HARDENED (2026-05-30, after the run-2 master brick).  Invariants — each one
// independently bricked the part or would let it execute corrupt code:
//   1. NO flash-resident calls in the window.  The old code called the flash
//      memcpy (sector 1); once sector 1 was erased the next call executed erased
//      flash → HardFault → vectored through the also-erased sector 0 → lockup.
//      We use ram_word_copy() (inlined) instead.  flash_range_erase/program are
//      verified RAM-resident and pre-resolve their ROM pointers (no lookup).
//   2. IRQs OFF for the ENTIRE copy.  This routine OVERWRITES THE RUNNING
//      FIRMWARE (offset 0).  Unlike flush_page (which writes the *staging*
//      region and may safely toggle IRQs between pages because the running code
//      stays intact), here we must NEVER return control to the running system
//      mid-copy — an IRQ/NMI would dispatch handler/thread code out of a sector
//      we have already erased or half-rewritten.  So: one save_and_disable
//      around the whole loop.  With core1 PSM-halted and no flash execution by
//      this function, PRIMASK=1 then blocks the only real exception sources
//      (Vector80→NMI can't fire; no HardFault because nothing runs from flash).
//   3. Sector 0 (vectors + HardFault/NMI handlers) written LAST — defensive:
//      minimises the span of time a valid vector table doesn't exist, in case
//      some unforeseen HardFault fires despite (2).
// ---------------------------------------------------------------------------
static void __no_inline_not_in_flash_func(fw_staging_do_apply)(uint32_t image_size) {
#ifdef USE_CORE1
    // Halt core1 via PSM reset.  _PSM_FRCE_OFF / _PSM_PROC1_BIT are #defines
    // (pure preprocessor text substitution → an inlined register write, no flash
    // fetch), so this is byte-for-byte identical machine code to the raw literal
    // it replaces — same as fw_staging_halt_core1(), inlined here for the
    // not-in-flash apply path.
    _PSM_FRCE_OFF |= _PSM_PROC1_BIT;
    __asm volatile ("dsb" ::: "memory");
    // The DSB only orders the store. Wait for the reset to actually land before any
    // flash operation, or core1 is still fetching from XIP when the QSPI leaves XIP
    // mode and the bus stalls for good. This is the one unsynchronised step in the
    // whole apply, and a few-cycle window is exactly why it presented as one half
    // completing while the other wedged on identical code (field, 2026-08-31).
    const uint32_t psm_spins = psm_wait_proc1_off();
    _WD_SCRATCH2 = psm_spins;
#endif
    dma_abort_all();
    // ⚠️ uint32_t, NOT uint8_t, and the type is the whole fix. ram_word_copy() below
    // copies words, so the compiler emits ldmia/stmia -- and an unaligned STMIA is a
    // HardFault on Cortex-M0+, taken with PRIMASK set inside a function that never
    // returns, i.e. an instant lockup with no trace. A uint8_t[256] has alignment 1,
    // so the linker packs it at whatever byte offset the previous .bss symbol leaves.
    // That is what bricked the HID firmware update: adding the macro label cache to
    // .bss shifted this buffer to ...ce3, and the first page of the first sector
    // faulted the core. The applier's CODE was byte-identical across the regression;
    // only its buffer moved, which is why a disassembly diff found nothing.
    // Declaring it as words means no cast can silently reintroduce the hazard.
    static uint32_t page_buf[FLASH_PAGE_SIZE / 4];   // static (.bss): never on a moving stack
    uint32_t num_sectors = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uint32_t pages = FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE;

    // IRQs OFF for the entire copy — see invariant (2).  Never restored; we reset.
    (void)save_and_disable_interrupts();

    // Erase+program one sector from staging.  Reads source via XIP (re-enabled by
    // flash_range_erase on return) into the RAM page buffer BEFORE program() drops
    // XIP again.  Staging (≥1 MB) is never erased here, so it stays XIP-readable.
    // One log page: word0 = marker, word1 = a witness value worth capturing.
    #define LOG_MARK(page, marker, witness)                                               \
        do {                                                                              \
            page_buf[0] = (marker);                                 \
            page_buf[1] = (witness);                                \
            flash_range_program(FW_APPLY_LOG_OFFSET + (page) * FLASH_PAGE_SIZE,           \
                                (const uint8_t *)page_buf, FLASH_PAGE_SIZE);                               \
        } while (0)

    #define APPLY_ONE_SECTOR(sec)                                                         \
        do {                                                                              \
            _WD_SCRATCH0 = FW_APPLY_CRUMB_SECTOR | ((sec) & 0xFFFFu);                     \
            uint32_t _dst = (sec) * FLASH_SECTOR_SIZE;                                    \
            uint32_t _src = XIP_BASE + FW_STAGING_DATA_OFFSET + (sec) * FLASH_SECTOR_SIZE;\
            if (_first) LOG_MARK(FW_APPLY_LOG_PAGE_PRE, FW_APPLY_LOG_PRE_ERASE, _dst);    \
            flash_range_erase(_dst, FLASH_SECTOR_SIZE);                                   \
            if (_first) LOG_MARK(FW_APPLY_LOG_POST, FW_APPLY_LOG_POST_ERASE,              \
                                 *(const volatile uint32_t *)(XIP_BASE + _dst));          \
            for (uint32_t _pg = 0; _pg < pages; _pg++) {                                  \
                ram_word_copy(page_buf,                                                   \
                              (const uint32_t *)(_src + _pg * FLASH_PAGE_SIZE),            \
                              FLASH_PAGE_SIZE);                                           \
                uint32_t _w0 = page_buf[0];                                               \
                flash_range_program(_dst + _pg * FLASH_PAGE_SIZE,                          \
                                    (const uint8_t *)page_buf, FLASH_PAGE_SIZE);           \
                if (_first && _pg == 0) LOG_MARK(FW_APPLY_LOG_PAGE_READ,                  \
                                                 FW_APPLY_LOG_POST_READ, _w0);            \
                if (_first && _pg == 0) {                                                 \
                    LOG_MARK(FW_APPLY_LOG_PAGE_PROG, FW_APPLY_LOG_POST_PROG,              \
                             *(const volatile uint32_t *)(XIP_BASE + _dst));              \
                    _first = false;                                                       \
                }                                                                         \
            }                                                                             \
            _first = false;                                                               \
        } while (0)

    // Fresh progress log. Safe to erase: it is not the running firmware, and the
    // static assert above proves no accepted image can reach it.
    for (uint32_t i = 0; i < FW_APPLY_LOG_SECTORS; i++) {
        flash_range_erase(FW_APPLY_LOG_OFFSET + i * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    }
    // ⚠️ Bounded by the log's own page count, not by the image's sector count: the
    // page index IS the sector number, and a >512 KB image has more sectors than
    // the log has pages, so an unbounded index would run straight off the end of
    // the log and into whatever follows it.
    #define LOG_SECTOR_DONE(sec)                                                          \
        do {                                                                              \
            if ((sec) >= FW_APPLY_LOG_PAGES) break;                                       \
            page_buf[0] = FW_APPLY_LOG_MAGIC | ((sec) & 0xFFFFu);   \
            flash_range_program(FW_APPLY_LOG_OFFSET + (sec) * FLASH_PAGE_SIZE,            \
                                (const uint8_t *)page_buf, FLASH_PAGE_SIZE);                               \
        } while (0)

    // The log is erased and its start marker written BEFORE any sector of the running
    // image is touched, so an empty log on the next boot means the applier never got
    // this far -- distinguishing "died erasing the first sector" from "died before the
    // copy began at all", which are different bugs and were previously the same symptom.
    bool _first = true;   // bracket only the first image sector
    page_buf[0] = FW_APPLY_LOG_START;
    flash_range_program(FW_APPLY_LOG_OFFSET + (FW_APPLY_LOG_PAGES - 1) * FLASH_PAGE_SIZE,
                        (const uint8_t *)page_buf, FLASH_PAGE_SIZE);

    // App body first: sectors 1..N-1, leaving sector 0's vectors written last.
    for (uint32_t sec = 1; sec < num_sectors; sec++) {
        APPLY_ONE_SECTOR(sec);
        LOG_SECTOR_DONE(sec);
    }
    if (num_sectors > 0) {
        APPLY_ONE_SECTOR(0);   // vectors LAST
        LOG_SECTOR_DONE(0);
    }
    #undef LOG_SECTOR_DONE
    #undef APPLY_ONE_SECTOR
    _WD_SCRATCH1 = FW_APPLY_CRUMB_DONE;   // the copy finished; anything after this is the reset

    // ...and the same fact in FLASH, because the scratch register is useless in the
    // case that matters: a half that never reboots, or reboots into an image that
    // cannot print, can never read it back -- so "hung mid-copy" and "copied fine,
    // booted dead" look identical from the outside. This page is in the staging
    // header sector, erased by every BEGIN and otherwise unused, so it survives a
    // power cycle and a BOOTSEL recovery and the NEXT firmware can report it.
    // ⚠️ Do NOT clear page_buf first. A `for` loop filling it is recognised by GCC and
    // turned into a call to memset -- which lives in FLASH, inside the region this
    // routine has just erased. That is invariant (1) above, and it is what bricked the
    // part in run 2. Only the first four words are read back; the rest of the page is
    // residue from the last copied page, which nothing ever looks at.
    // Check our own work before resetting into it. The source has already been
    // verified byte-correct in flash (APPLY 3/4), so comparing the region we just
    // wrote against that same source answers the one remaining question -- did the
    // copy put the right bytes down? -- and it answers it in a place that survives
    // the BOOTSEL recovery, which is the only way back from a bad apply.
    //
    // volatile so GCC cannot turn this into a memcmp() call: that lives in flash,
    // inside the region just erased and rewritten, and calling into it here is the
    // invariant-(1) violation that bricked the part in run 2.
    volatile const uint32_t *wrote = (volatile const uint32_t *)(XIP_BASE);
    volatile const uint32_t *want  = (volatile const uint32_t *)(XIP_BASE + FW_STAGING_DATA_OFFSET);
    uint32_t diff_at = 0xFFFFFFFFu, got_w = 0, want_w = 0;
    for (uint32_t w = 0; w < image_size / 4u; w++) {
        if (wrote[w] != want[w]) {
            diff_at = w * 4u;
            got_w   = wrote[w];
            want_w  = want[w];
            break;
        }
    }

    page_buf[0] = FW_APPLY_DONE_MAGIC;
    page_buf[1] = num_sectors;
    page_buf[2] = image_size;
    page_buf[3] = psm_spins;
    page_buf[4] = diff_at;
    page_buf[5] = got_w;
    page_buf[6] = want_w;
    flash_range_program(FW_STAGING_OFFSET + FLASH_PAGE_SIZE, (const uint8_t *)page_buf, FLASH_PAGE_SIZE);

    // Full-chip reset via the watchdog — NOT NVIC_SystemReset().  NVIC_SystemReset
    // only resets the M0+ core (AIRCR.SYSRESETREQ) and leaves USB/PLL/peripheral
    // state behind, which hangs at early boot until a replug (same warm-reset bug
    // the QK_REBOOT keycode hits; see poly_util.c mcu_reset()).  We can't call the
    // flash-resident watchdog_reboot() from here (invariant 1: no flash calls in
    // the apply window), so we inline the same effect with register writes:
    //   PSM_WDSEL  = reset everything except ROSC/XOSC   (0x1ffff & ~0x3 = 0x1fffc)
    //   WATCHDOG_SCRATCH4 = 0  → bootrom takes the normal flash boot path
    //   WATCHDOG_CTRL |= TRIGGER → immediate full reset
    // (RP2040 datasheet §4.7; mirrors pico-sdk watchdog_reboot(0,0,0).)
#ifdef USE_CORE1
    // Release core1 from the PSM force-off asserted at the top of this function,
    // so it is NOT left held in reset across the watchdog reboot.  The watchdog
    // reset does not clear the PSM control registers, so a leftover FRCE_OFF.PROC1
    // would keep core1 dead after the reboot — and post_init's
    // multicore_launch_core1() FIFO handshake would then block forever (core1
    // never drains the FIFO), hanging the keyboard on the boot splash.  Clearing
    // it here only sends core1 back to the bootrom FIFO-wait loop; it does NOT
    // run the just-rewritten flash.
    *(volatile uint32_t *)(0x40010000u + 0x3000u + 0x04u) = (1u << 16);  // PSM FRCE_OFF CLR, PROC1
    __asm volatile ("dsb" ::: "memory");
#endif
    _PSM_WDSEL    = 0x0001ffffu & ~0x00000003u;   // all blocks except ROSC(0x1)+XOSC(0x2)
    _WD_SCRATCH4  = 0u;                            // normal flash boot, not reboot-to-addr
    _WD_CTRL     |= 0x80000000u;                   // WATCHDOG_CTRL_TRIGGER_BITS
    __asm volatile ("dsb" ::: "memory");
    while (1) { /* wait for the watchdog reset to take effect */ }
}

// Re-CRC the staged bytes AS THEY SIT IN FLASH.
//
// COMMIT does not do this and never has: s_staged_crc is accumulated in
// fw_staging_write_chunk() over the bytes as they ARRIVE, in RAM. That proves the
// host's image reached us intact; it proves nothing about what landed in flash. The
// applier is about to erase the only working copy of the firmware on the strength of
// it, so verify the actual source first and refuse if it does not match -- a refusal
// leaves a working keyboard, writing an unverified image does not.
//
// Affordable here (~25 ms over ~490 KB) precisely because this runs from housekeeping.
// The same scan inside COMMIT overflowed the split-transaction window on the slave,
// which is why finalize keeps the O(1) running CRC -- do not move this there.
bool fw_staging_verify_staged_flash(uint32_t *size, uint32_t *expect_crc, uint32_t *actual_crc) {
    const uint32_t *hdr = (const uint32_t *)(XIP_BASE + FW_STAGING_OFFSET);
    if (hdr[0] != FW_STAGING_MAGIC) return false;
    // ⚠️ crc32_large(), NOT crc32_1byte(): that one takes a uint16_t length, so a
    // ~490 KB image silently truncates to (size & 0xFFFF) and the CRC is computed over
    // the first few percent of the file. It then never matches, so the verify below
    // refuses EVERY image over 64 KB -- which is exactly what it did on its first
    // outing (reported c48f3db3, the CRC of the leading 34164 bytes of a 492916-byte
    // image). The helper exists for this and says so in its own comment.
    const uint32_t crc = crc32_large((const uint8_t *)(XIP_BASE + FW_STAGING_DATA_OFFSET),
                                     hdr[1]);
    if (size)       *size       = hdr[1];
    if (expect_crc) *expect_crc = hdr[2];
    if (actual_crc) *actual_crc = crc;
    return crc == hdr[2];
}

void fw_staging_cancel_apply(void) {
    s_commit_pending = false;
}

void fw_staging_apply_and_reboot(void) {
    const uint32_t *hdr = (const uint32_t *)(XIP_BASE + FW_STAGING_OFFSET);
    if (hdr[0] != FW_STAGING_MAGIC) {
        // ⚠️ This used to be a bare `return`, which left s_commit_pending SET — so
        // housekeeping re-entered the WHOLE apply block on every pass, forever:
        // clear_keyboard() (board dead), poly_flash_rgb_now() (orange latched),
        // oled_fw_apply_screen() ("Applying") and save_all_dirty(). From the outside
        // that is indistinguishable from a half-written flash, and nothing anywhere
        // said why. Report it and disarm so the board stays usable.
        uprintf("FWAPPLY refused: no staged image (magic %08lx)\n", (unsigned long)hdr[0]);
        s_commit_pending = false;
        return;
    }

    // The size the applier trusts, printed BEFORE the blocking self-flash — this is
    // the last thing the console can ever see from this build (the copy disables
    // interrupts, never returns, and console_task() never runs again). The filler
    // line that follows is not decoration: QMK's console only ships FULL 32-byte HID
    // reports and the remainder waits for a main loop that is about to stop existing,
    // so without it the tail of the real line is lost.
    if (!fw_staging_verify_staged_flash(NULL, NULL, NULL)) {
        uprintf("FWAPPLY refused: staged image FAILED its flash CRC - not overwriting\n");
        s_commit_pending = false;
        return;
    }

    const uint32_t sectors = (hdr[1] + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uprintf("FWAPPLY sectors=%lu size=%lu crc=%08lx\n",
            (unsigned long)sectors, (unsigned long)hdr[1], (unsigned long)hdr[2]);
    uprint("FWAPPLY ------------------------------------------------------------\n");

    s_commit_pending = false;
    // The copy below runs with interrupts off for SECONDS (erase + program of the
    // whole image) and never returns: a still-armed watchdog would reset the chip
    // mid-write, and the reset would be archived as a crash. Disarm it here, in
    // flash-resident code, before the RAM-resident applier takes over.
    (void)crash_phase_enter(CRASH_PHASE_APPLY, 0);
    crash_watchdog_stop();
    fw_staging_do_apply(hdr[1]);   // never returns
}
