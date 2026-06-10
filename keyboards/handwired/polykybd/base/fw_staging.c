// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"

#include "fw_staging.h"
#include "polymod_crc32.h"

#include "hardware/flash.h"
#include "hardware/sync.h"

#include <string.h>

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
#include "base/multicore/core1.h"

#define _PSM_FRCE_OFF  (*(volatile uint32_t *)0x40010004u)
#define _PSM_PROC1_BIT (1u << 16)

// True while this module holds a PSM reset on core1.
static bool s_core1_halted = false;

static void fw_staging_halt_core1(void) {
    _PSM_FRCE_OFF |= _PSM_PROC1_BIT;
    // FRCE_OFF takes effect within a few cycles; DSB ensures the write
    // reaches the PSM peripheral before the caller touches flash.
    __asm volatile ("dsb" ::: "memory");
    s_core1_halted = true;
}

static void fw_staging_restart_core1(void) {
    _PSM_FRCE_OFF &= ~_PSM_PROC1_BIT;
    s_core1_halted = false;
    multicore_launch_core1();
}
#endif

// ---------------------------------------------------------------------------
// Linker-exported firmware boundary symbols
// ---------------------------------------------------------------------------
extern uint8_t __flash_binary_start;
extern uint8_t __flash_binary_end;

static bool     s_initialized = false;
static bool     s_fw_up_active = false;

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

static bool     s_commit_pending;
static bool     s_reboot_pending;   // deferred plain reboot (QK_REBOOT slave path)

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
// Flush accumulated page buffer to staging flash
// ---------------------------------------------------------------------------
static void flush_page(void) {
    uint32_t flash_offs = FW_STAGING_DATA_OFFSET + (s_next_offset - s_buf_fill);
#ifdef USE_CORE1
    bool already_halted = s_core1_halted;
    if (!already_halted) fw_staging_halt_core1();
#endif
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(flash_offs, s_page_buf, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
#ifdef USE_CORE1
    if (!already_halted) fw_staging_restart_core1();
#endif
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
#ifdef USE_CORE1
    bool already_halted = s_core1_halted;
    if (!already_halted) fw_staging_halt_core1();
#endif
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(FW_STAGING_OFFSET, hdr, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
#ifdef USE_CORE1
    if (!already_halted) fw_staging_restart_core1();
#endif
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void fw_staging_init(void) {
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
#ifdef USE_CORE1
    s_core1_halted   = false;
#endif
}

// Synchronous begin: erases staging sector-by-sector with interrupts briefly
// re-enabled between sectors so USB and watchdog stay alive.  Use this on the
// master (USB side); the host app timeout must cover the full erase duration
// (~50 ms × ceil(image_size/4096) sectors).
void fw_staging_begin(uint32_t image_size, uint32_t image_crc) {
    if (!s_initialized) fw_staging_init();

    s_next_offset    = 0;
    s_buf_fill       = 0;
    s_commit_pending = false;
    s_erase_pending  = false;
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);

    if (image_size == 0 || image_size > FW_UP_MAX_SIZE) {
        s_image_size = 0;
        s_image_crc  = 0;
        return;
    }
    s_image_size = image_size;
    s_image_crc  = image_crc;
    s_fw_up_active = true;

    // Erase header sector then each data sector individually.
    // Re-enabling interrupts between sectors keeps USB/watchdog responsive
    // during the ~50 ms erase window per sector.
    uint32_t data_sectors = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uint32_t irq;

#ifdef USE_CORE1
    fw_staging_halt_core1();
#endif
    irq = save_and_disable_interrupts();
    flash_range_erase(FW_STAGING_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);

    for (uint32_t s = 0; s < data_sectors; s++) {
        irq = save_and_disable_interrupts();
        flash_range_erase(FW_STAGING_DATA_OFFSET + s * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
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
    if (!s_initialized) fw_staging_init();

    s_next_offset    = 0;
    s_buf_fill       = 0;
    s_staged_crc     = 0;
    s_commit_pending = false;
    memset(s_page_buf, 0xFF, FLASH_PAGE_SIZE);

    if (image_size == 0 || image_size > FW_UP_MAX_SIZE) {
        s_image_size    = 0;
        s_image_crc     = 0;
        s_erase_pending = false;
        return;
    }
    s_image_size = image_size;
    s_image_crc  = image_crc;
    s_fw_up_active = true;

    uint32_t data_sectors   = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    s_erase_sector_count    = 1 + data_sectors;  // index 0 = header, 1..N = data
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
    if (s_erase_sector_next == 0) {
        offset = FW_STAGING_OFFSET;   // header sector
    } else {
        offset = FW_STAGING_DATA_OFFSET + (s_erase_sector_next - 1) * FLASH_SECTOR_SIZE;
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

bool fw_staging_finalize(void) {
    if (!s_initialized) return false;

    // Flush any partial final page (padded with 0xFF already from fw_staging_begin/flush_page)
    if (s_buf_fill > 0) {
        flush_page();
    }

    // Verify using the running CRC accumulated in fw_staging_write_chunk (O(1)).
    // (Re-scanning the 244 KB staged region here overflowed the split-transaction
    // window and made the master mis-report COMMIT as "CRC mismatch" — run 6.)
    bool ok = (s_staged_crc == s_image_crc);

    // On a CRC match, stamp the staging header {magic,size,crc} so the staged image
    // is self-describing and fw_staging_apply_and_reboot() can validate + size it.
    if (ok) {
        write_staging_header(s_image_size, s_image_crc);
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
#endif
    static uint8_t page_buf[FLASH_PAGE_SIZE];   // static (.bss): never on a moving stack
    uint32_t num_sectors = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uint32_t pages = FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE;

    // IRQs OFF for the entire copy — see invariant (2).  Never restored; we reset.
    (void)save_and_disable_interrupts();

    // Erase+program one sector from staging.  Reads source via XIP (re-enabled by
    // flash_range_erase on return) into the RAM page buffer BEFORE program() drops
    // XIP again.  Staging (≥1 MB) is never erased here, so it stays XIP-readable.
    #define APPLY_ONE_SECTOR(sec)                                                         \
        do {                                                                              \
            uint32_t _dst = (sec) * FLASH_SECTOR_SIZE;                                    \
            uint32_t _src = XIP_BASE + FW_STAGING_DATA_OFFSET + (sec) * FLASH_SECTOR_SIZE;\
            flash_range_erase(_dst, FLASH_SECTOR_SIZE);                                   \
            for (uint32_t _pg = 0; _pg < pages; _pg++) {                                  \
                ram_word_copy((uint32_t *)page_buf,                                       \
                              (const uint32_t *)(_src + _pg * FLASH_PAGE_SIZE),            \
                              FLASH_PAGE_SIZE);                                           \
                flash_range_program(_dst + _pg * FLASH_PAGE_SIZE, page_buf, FLASH_PAGE_SIZE); \
            }                                                                             \
        } while (0)

    // App body first: sectors 1..N-1, leaving sector 0's vectors written last.
    for (uint32_t sec = 1; sec < num_sectors; sec++) {
        APPLY_ONE_SECTOR(sec);
    }
    if (num_sectors > 0) {
        APPLY_ONE_SECTOR(0);   // vectors LAST
    }
    #undef APPLY_ONE_SECTOR

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
    #define _PSM_WDSEL       (*(volatile uint32_t *)(0x40010000u + 0x08u))
    #define _WD_CTRL         (*(volatile uint32_t *)(0x40058000u + 0x00u))
    #define _WD_SCRATCH4     (*(volatile uint32_t *)(0x40058000u + 0x1cu))
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

void fw_staging_apply_and_reboot(void) {
    const uint32_t *hdr = (const uint32_t *)(XIP_BASE + FW_STAGING_OFFSET);
    if (hdr[0] != FW_STAGING_MAGIC) return;   // no valid staged image

    s_commit_pending = false;
    fw_staging_do_apply(hdr[1]);   // never returns
}
