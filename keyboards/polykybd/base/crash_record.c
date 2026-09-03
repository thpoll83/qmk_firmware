// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
// See crash_record.h for the design.
#include "crash_record.h"

#include "quantum.h"          // FW_VERSION
#include "print.h"
#include "fw_staging.h"       // FW_CRASH_LOG_OFFSET, fw_staging_core1_lockout_*()
#include "polymod_crc32.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/vreg_and_chip_reset.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/timer.h"

#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// The NOLOAD block. `.ram0.*` is placed after __ram0_noinit__ by ChibiOS's
// rules_memory.ld (both PolyKybd ldscripts include it), so crt0 neither loads
// nor clears it -- exactly how the double-tap bootloader keeps its magic word
// across a reset. Random after a power-on, hence the magic and the CRC.
// ---------------------------------------------------------------------------
#define CRASH_RAM_MAGIC 0x5AFEB007u

// Reboot at most this many times in a row on the strength of a crash record.
// Past it the fault handler records and then HALTS (today's behaviour), so a
// firmware that faults at every boot presents as a dead board rather than a
// keyboard that flickers through a reboot storm. The archive keeps the record.
#define CRASH_LOOP_LIMIT 5u

typedef struct {
    uint32_t          magic;
    uint32_t          consecutive;   // crashes in a row so far (0 after a clean boot)
    volatile uint16_t phase;
    volatile uint16_t phase_arg;
    poly_crash_record_t rec;         // rec.magic == CRASH_RECORD_MAGIC while a crash is pending
} crash_ram_t;

static crash_ram_t s_ram __attribute__((section(".ram0.crash_record"), aligned(4)));

// Per-boot state (ordinary .bss).
static bool                 s_fresh        = false;   // this boot captured a record
static bool                 s_have_archive = false;
static poly_crash_record_t  s_archive;                 // last archived record (flash copy)
static bool                 s_have_slave   = false;
static bool                 s_slave_fresh  = false;
static poly_crash_record_t  s_slave;                   // last record pulled from the slave

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static uint32_t rec_crc(const poly_crash_record_t *r) {
    return crc32_1byte(r, (uint16_t)offsetof(poly_crash_record_t, crc), 0);
}

static bool rec_valid(const poly_crash_record_t *r) {
    return r->magic == CRASH_RECORD_MAGIC && r->crc == rec_crc(r);
}

static void ram_block_reset(void) {
    memset(&s_ram, 0, sizeof(s_ram));
    s_ram.magic = CRASH_RAM_MAGIC;
    s_ram.phase = CRASH_PHASE_BOOT;
}

static uint32_t uptime_ms_raw(void) {
    // The hardware timer, not QMK's timer_read32(): this runs inside a fault
    // handler where the OS tick may be exactly what is broken.
    return timer_hw->timerawl / 1000u;
}

// A stack pointer that does not point into SRAM (a stack overflow, a corrupted
// PSP) must not be dereferenced from inside the fault handler -- that would fault
// again and lock the core up with the record half-written.
static bool frame_readable(const uint32_t *frame) {
    uint32_t a = (uint32_t)frame;
    return frame != NULL && a >= 0x20000000u && a + 32u <= 0x20042000u && (a & 3u) == 0u;
}

static void copy_fw(char *dst) {
    const char *v = FW_VERSION;
    size_t      n = strlen(v);
    if (n > CRASH_FW_LEN) n = CRASH_FW_LEN;
    memset(dst, 0, CRASH_FW_LEN);
    memcpy(dst, v, n);
}

// Fill the record fields shared by every kind. Interrupts are assumed OFF.
static void rec_fill(poly_crash_record_t *r, uint8_t kind, const uint32_t *frame, uint32_t sp) {
    memset(r, 0, sizeof(*r));
    r->magic     = CRASH_RECORD_MAGIC;
    r->kind      = kind;
    r->core      = (uint8_t)sio_hw->cpuid;
    r->consecutive = (uint8_t)((s_ram.magic == CRASH_RAM_MAGIC ? s_ram.consecutive : 0u) + 1u);
    if (frame_readable(frame)) {
        // Cortex-M exception frame: r0 r1 r2 r3 r12 lr pc xpsr
        r->lr   = frame[5];
        r->pc   = frame[6];
        r->xpsr = frame[7];
    }
    r->sp        = sp;
    r->icsr      = scb_hw->icsr;
    r->uptime_ms = uptime_ms_raw();
    r->phase     = (s_ram.magic == CRASH_RAM_MAGIC) ? s_ram.phase : CRASH_PHASE_UNKNOWN;
    r->phase_arg = (s_ram.magic == CRASH_RAM_MAGIC) ? s_ram.phase_arg : 0;
    copy_fw(r->fw);
    r->crc = rec_crc(r);
}

// Record into the NOLOAD block and reboot the whole chip. Never returns.
static void __attribute__((noreturn)) record_and_reboot(uint8_t kind, const uint32_t *frame, uint32_t sp) {
    __asm volatile("cpsid i" ::: "memory");
    if (s_ram.magic != CRASH_RAM_MAGIC) {
        // Power-on garbage or a fault before init: start the block fresh so the
        // record at least carries a sane counter.
        memset(&s_ram, 0, sizeof(s_ram));
        s_ram.magic = CRASH_RAM_MAGIC;
    }
    rec_fill(&s_ram.rec, kind, frame, sp);
    __asm volatile("dsb" ::: "memory");
    if (s_ram.rec.consecutive > CRASH_LOOP_LIMIT) {
        // Crash loop: stop rebooting. The record is in RAM for the next
        // RUN-pin / watchdog reset to archive, and the previous ones are in flash.
        while (1) { __asm volatile("wfi"); }
    }
    // Full-chip reset via the watchdog (what mcu_reset() does) -- a plain
    // NVIC_SystemReset leaves peripherals half-reset on the RP2040 and hangs at
    // early boot (see poly_util.c). scratch4 = 0 -> normal flash boot.
    watchdog_reboot(0, 0, 0);
    while (1) { }
}

// ---------------------------------------------------------------------------
// The fault handlers
// ---------------------------------------------------------------------------

// Called from HardFault_Handler with the active stack's frame and EXC_RETURN.
void crash_record_fault(const uint32_t *frame, uint32_t exc_return) __attribute__((noreturn, used));
void crash_record_fault(const uint32_t *frame, uint32_t exc_return) {
    (void)exc_return;
    record_and_reboot(CRASH_KIND_HARDFAULT, frame, (uint32_t)frame);
}

// ChibiOS's vectors.S declares HardFault_Handler weak (a `b .` loop); this strong
// definition takes the vector on BOTH cores (core1 launches on the same table).
// Naked: pick the stack the frame is on from EXC_RETURN bit 2, then tail-call.
__attribute__((naked, noreturn)) void HardFault_Handler(void) {
    __asm volatile(
        "movs r0, #4          \n"
        "mov  r1, lr          \n"
        "tst  r0, r1          \n"
        "beq  1f              \n"
        "mrs  r0, psp         \n"
        "b    2f              \n"
        "1: mrs r0, msp       \n"
        "2: mov r1, lr        \n"
        "ldr  r2, =crash_record_fault \n"
        "bx   r2              \n"
        ".ltorg               \n");
}

// Every other unhandled vector (SVC, PendSV, SysTick, an unused IRQ) funnels
// through vectors.S's shared body, which `bl`s here -- so EXC_RETURN is gone,
// but the frame is still on whichever stack was active. Prefer the process
// stack when it points into SRAM (ChibiOS runs threads on PSP); the vector
// number in ICSR is the reliable part of this record either way.
void crash_record_unhandled(uint32_t msp, uint32_t psp) __attribute__((noreturn, used));
void crash_record_unhandled(uint32_t msp, uint32_t psp) {
    const bool psp_sane = (psp >= 0x20000000u) && (psp < 0x20042000u) && ((psp & 3u) == 0u);
    uint32_t   sp       = psp_sane ? psp : msp;
    record_and_reboot(CRASH_KIND_UNHANDLED, (const uint32_t *)sp, sp);
}

__attribute__((naked, noreturn)) void _unhandled_exception(void) {
    __asm volatile(
        "mrs  r0, msp         \n"
        "mrs  r1, psp         \n"
        "ldr  r2, =crash_record_unhandled \n"
        "bx   r2              \n"
        ".ltorg               \n");
}

void crash_record_halt(uint16_t arg) {
    // A deliberate stop: no frame, but keep the caller's own tag as the detail.
    if (s_ram.magic == CRASH_RAM_MAGIC) s_ram.phase_arg = arg;
    record_and_reboot(CRASH_KIND_HALT, NULL, 0);
}

// ---------------------------------------------------------------------------
// The phase breadcrumb
// ---------------------------------------------------------------------------
uint32_t crash_phase_enter(uint16_t phase, uint16_t arg) {
    uint32_t prev = ((uint32_t)s_ram.phase << 16) | s_ram.phase_arg;
    s_ram.phase_arg = arg;
    s_ram.phase     = phase;
    return prev;
}

void crash_phase_leave(uint32_t prev) {
    s_ram.phase_arg = (uint16_t)(prev & 0xFFFFu);
    s_ram.phase     = (uint16_t)(prev >> 16);
}

// ---------------------------------------------------------------------------
// The flash archive: one 4 KB sector, appended a 256-byte page at a time, erased
// and restarted when full. Reading is "the last valid page"; writing is "the
// first erased page". A crash loop of any length therefore costs one erase per
// 16 crashes, not one per boot.
// ---------------------------------------------------------------------------
#define ARCHIVE_PAGES (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)

static const poly_crash_record_t *archive_page(uint32_t i) {
    return (const poly_crash_record_t *)(XIP_BASE + FW_CRASH_LOG_OFFSET + i * FLASH_PAGE_SIZE);
}

static bool page_erased(uint32_t i) {
    return archive_page(i)->magic == 0xFFFFFFFFu;
}

// A record init captured but has not yet written to flash (see the header).
static bool s_archive_pending = false;

static void archive_scan(void) {
    s_have_archive = false;
    for (uint32_t i = 0; i < ARCHIVE_PAGES; i++) {
        const poly_crash_record_t *p = archive_page(i);
        if (page_erased(i)) break;
        if (rec_valid(p)) {
            s_archive      = *p;
            s_have_archive = true;
        }
    }
}

static void flash_guarded(bool erase, uint32_t off, const uint8_t *page) {
    uint32_t tag = crash_phase_enter(CRASH_PHASE_FLASH, erase ? 2 : 1);
    fw_staging_core1_lockout_begin();
    uint32_t irq = save_and_disable_interrupts();
    if (erase) {
        flash_range_erase(off, FLASH_SECTOR_SIZE);
    } else {
        flash_range_program(off, page, FLASH_PAGE_SIZE);
    }
    restore_interrupts(irq);
    fw_staging_core1_lockout_end();
    crash_phase_leave(tag);
}

static void archive_append(const poly_crash_record_t *rec) {
    uint32_t slot = ARCHIVE_PAGES;
    for (uint32_t i = 0; i < ARCHIVE_PAGES; i++) {
        if (page_erased(i)) { slot = i; break; }
    }
    if (slot == ARCHIVE_PAGES) {
        flash_guarded(true, FW_CRASH_LOG_OFFSET, NULL);
        slot = 0;
    }
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, rec, sizeof(*rec));
    flash_guarded(false, FW_CRASH_LOG_OFFSET + slot * FLASH_PAGE_SIZE, page);
    s_archive      = *rec;
    s_have_archive = true;
}

// ---------------------------------------------------------------------------
// Boot-time capture
// ---------------------------------------------------------------------------
static uint8_t read_reset_reason(void) {
    uint8_t  bits = 0;
    uint32_t chip = vreg_and_chip_reset_hw->chip_reset;
    if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS)         bits |= CRASH_RESET_HAD_POR;
    if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS)         bits |= CRASH_RESET_HAD_RUN;
    if (chip & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS) bits |= CRASH_RESET_HAD_PSM;
    uint32_t wd = watchdog_hw->reason;
    if (wd & WATCHDOG_REASON_TIMER_BITS) bits |= CRASH_RESET_WD_TIMER;
    if (wd & WATCHDOG_REASON_FORCE_BITS) bits |= CRASH_RESET_WD_FORCE;
    return bits;
}

void crash_record_init(void) {
    const uint8_t reason = read_reset_reason();
    // Zeroed at declaration: only the `have` branches fill it, and cppcheck
    // cannot see that guard (uninitStructMember).
    poly_crash_record_t captured;
    memset(&captured, 0, sizeof(captured));
    bool have = false;

    if (s_ram.magic != CRASH_RAM_MAGIC) {
        // Power-on (or the first boot of this firmware): nothing to report.
        ram_block_reset();
    } else if (rec_valid(&s_ram.rec)) {
        captured = s_ram.rec;
        have     = true;
    } else if (reason & CRASH_RESET_WD_TIMER) {
        // The loop stopped feeding the watchdog and nothing recorded why: a hang.
        // Synthesise a record from the breadcrumb the loop left behind -- that
        // tag is the entire diagnosis of a hang, since no frame exists.
        captured.magic       = CRASH_RECORD_MAGIC;
        captured.kind        = CRASH_KIND_WATCHDOG;
        captured.core        = 0;
        captured.consecutive = (uint8_t)(s_ram.consecutive + 1u);
        captured.phase       = s_ram.phase;
        captured.phase_arg   = s_ram.phase_arg;
        copy_fw(captured.fw);
        have = true;
    }

    if (have) {
        captured.reset_reason = reason;
        captured.crc          = rec_crc(&captured);
        s_ram.consecutive     = captured.consecutive;
        s_fresh               = true;
    } else {
        s_ram.consecutive = 0;
    }
    // The RAM copy has done its job either way; a stale one must never be
    // re-reported by a later boot.
    memset(&s_ram.rec, 0, sizeof(s_ram.rec));
    s_ram.phase     = CRASH_PHASE_BOOT;
    s_ram.phase_arg = 0;

    archive_scan();
    if (have) {
        // Remembered now, written by crash_record_archive_pending() once core1 is
        // up -- the flash write must not run before the launch (see the header).
        // Bounded: a firmware that crashes at every boot would otherwise rewrite
        // the same story once per boot. The first CRASH_LOOP_LIMIT go in.
        s_archive         = captured;
        s_have_archive    = true;
        s_archive_pending = captured.consecutive <= CRASH_LOOP_LIMIT;
    }
}

void crash_record_archive_pending(void) {
    if (!s_archive_pending) return;
    s_archive_pending = false;
    archive_append(&s_archive);
}

bool crash_record_fresh(void) {
    return s_fresh;
}

bool crash_record_archived(poly_crash_record_t *out) {
    if (!s_have_archive) return false;
    if (out) *out = s_archive;
    return true;
}

void crash_record_clear(void) {
    if (!page_erased(0)) {
        flash_guarded(true, FW_CRASH_LOG_OFFSET, NULL);
    }
    s_have_archive = false;
    s_fresh        = false;
    s_have_slave   = false;
    s_slave_fresh  = false;
    memset(&s_archive, 0, sizeof(s_archive));
    memset(&s_slave, 0, sizeof(s_slave));
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------
static const char *kind_name(uint8_t kind) {
    switch (kind) {
        case CRASH_KIND_HARDFAULT: return "hardfault";
        case CRASH_KIND_UNHANDLED: return "unhandled";
        case CRASH_KIND_WATCHDOG:  return "watchdog";
        case CRASH_KIND_HALT:      return "halt";
        default:                   return "none";
    }
}

int crash_record_format(const poly_crash_record_t *rec, const char *side, char *buf, size_t n) {
    char fw[CRASH_FW_LEN + 1];
    memcpy(fw, rec->fw, CRASH_FW_LEN);
    fw[CRASH_FW_LEN] = '\0';
    return snprintf(buf, n,
                    "crash: side=%s kind=%s core=%u pc=0x%08lx lr=0x%08lx sp=0x%08lx "
                    "psr=0x%08lx icsr=0x%08lx phase=%u:0x%04x up=%lums n=%u reason=0x%02x fw=%s",
                    side, kind_name(rec->kind), (unsigned)rec->core,
                    (unsigned long)rec->pc, (unsigned long)rec->lr, (unsigned long)rec->sp,
                    (unsigned long)rec->xpsr, (unsigned long)rec->icsr,
                    (unsigned)rec->phase, (unsigned)rec->phase_arg,
                    (unsigned long)rec->uptime_ms, (unsigned)rec->consecutive,
                    (unsigned)rec->reset_reason, fw);
}

static void emit_one(const poly_crash_record_t *rec, const char *side) {
    char line[192];
    crash_record_format(rec, side, line, sizeof(line));
    uprintf("   %s\n", line);
}

void crash_record_emit_lines(void) {
    // Only a FRESH record is announced: the archive is a history for polyctl /
    // the HID command, not something to re-report at every boot forever.
    if (s_fresh && s_have_archive) emit_one(&s_archive, "master");
    // The slave's line is printed ONCE, at the pull (crash_record_note_slave):
    // the link can come up long after the banner re-emits have stopped.
}

static uint8_t fill_body(uint8_t *out, uint8_t out_len, bool present, bool fresh,
                         const poly_crash_record_t *rec) {
    if (out_len < CRASH_HID_BODY_LEN) return 0;
    out[0] = (present ? CRASH_HID_FLAG_PRESENT : 0) | (fresh ? CRASH_HID_FLAG_FRESH : 0);
    if (present) {
        memcpy(&out[1], rec, sizeof(*rec));
    } else {
        memset(&out[1], 0, sizeof(*rec));
    }
    return (uint8_t)CRASH_HID_BODY_LEN;
}

uint8_t crash_record_hid_body(uint8_t which, uint8_t *out, uint8_t out_len) {
    if (which == 1) {
        return fill_body(out, out_len, s_have_slave, s_slave_fresh, &s_slave);
    }
    return fill_body(out, out_len, s_have_archive, s_fresh, &s_archive);
}

// ---------------------------------------------------------------------------
// The split link
// ---------------------------------------------------------------------------
void crash_record_slave_reply(uint8_t *out, uint8_t out_len) {
    (void)fill_body(out, out_len, s_have_archive, s_fresh, &s_archive);
}

bool crash_record_note_slave(const uint8_t *body, uint8_t len) {
    if (len < CRASH_HID_BODY_LEN) return false;
    if (!(body[0] & CRASH_HID_FLAG_PRESENT)) {
        s_have_slave = false;
        return true;
    }
    poly_crash_record_t rec;
    memcpy(&rec, &body[1], sizeof(rec));
    if (!rec_valid(&rec)) return false;
    s_slave       = rec;
    s_have_slave  = true;
    s_slave_fresh = (body[0] & CRASH_HID_FLAG_FRESH) != 0;
    if (s_slave_fresh) emit_one(&s_slave, "slave");
    return true;
}

// ---------------------------------------------------------------------------
// The watchdog
// ---------------------------------------------------------------------------
void crash_watchdog_start(void) {
    s_ram.phase = CRASH_PHASE_LOOP;
    watchdog_enable(CRASH_WATCHDOG_MS, true);
}

void crash_watchdog_feed(void) {
    watchdog_update();
}

void crash_watchdog_stop(void) {
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
}
