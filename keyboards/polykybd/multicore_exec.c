// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "multicore_exec.h"
#include "base/crash_record.h"
#include "polykybd.h"

#include "print.h"
#include "config.h"
#include "base/helpers.h"
#include "base/update.h"
#include "polymod_rle.h"
#include "base/disp_array.h"
#include "polymod_core1.h"
#include "fill_overlay.h"   // for mark_display_has_overlay_post_upload
#include "anim/startup_anim.h"   // startup_anim_core1_job (the Eden idle keycap job)
#include "doom/doom_mode.h"      // doom_mode_active: DOOM owns core1 while it runs
#include "base/core1_owner.h"    // core1_held/hold/release: the one owner of core1
#include "base/core1_suspend.h"  // core1_suspend_step/core1_resume_step: the hold across a USB suspend

#ifdef USE_CORE1
#include "pico/platform.h"   // __no_inline_not_in_flash_func
#include "hardware/structs/timer.h"   // timer_hw: a clock that runs with interrupts off

#include "hardware/sync.h"            // save_and_disable_interrupts / restore_interrupts
#include "wait.h"                     // wait_ms: a recovering hand-over sleeps, never spins

// The word core0 sends with interrupts off before an EEPROM write. core1 answers it from
// core1_fifo_pop_ram() and stays there; it never reaches core1_entry()'s switch.
#define CORE1_CMD_PARK 0xcafe0005u

// Incremented by core1, in RAM, each time it takes CORE1_CMD_PARK off the FIFO.
static volatile uint32_t s_c1_park_acks = 0;

// core1's wait for its next command, in RAM. Idle core1 spends nearly all its time here,
// and an EEPROM write (a flash program or erase on core0) flushes the XIP cache while
// the QSPI is out of XIP mode: a wait loop in flash then faults or stalls on its next
// fetch (fw_staging.c, "Dual-core flash safety"). From RAM, an idle core1 cannot touch
// flash however long the write takes.
//
// It also answers CORE1_CMD_PARK without leaving RAM. FIFO order means core1 has
// finished every earlier command, returned from its flash code and come back here
// before it reads PARK. So an acknowledged PARK proves core1 is waiting in RAM with
// nothing queued, which "the job counters match" does not (a job publishes its
// completion before it returns through core1_entry(), in flash).
static uint32_t __no_inline_not_in_flash_func(core1_fifo_pop_ram)(void) {
    for (;;) {
        while (!(sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)) {
            __asm volatile("wfe");
        }
        const uint32_t word = sio_hw->fifo_rd;
        if (word != CORE1_CMD_PARK) {
            return word;
        }
        s_c1_park_acks = s_c1_park_acks + 1u;
    }
}

// Set from a forced core1 reset during an EEPROM write until the relaunch after it.
// Every hand-over of work to core1 waits on it (core1_wait_recovered): on the slave a
// split-thread hand-over could otherwise spin on a core1 in reset while the thread
// that relaunches it never runs.
static volatile bool s_c1_recovering = false;

static void core1_wait_recovered(void) {
    while (s_c1_recovering) {
        wait_ms(1);
    }
}

static volatile uint16_t core1_bit_index = 0;
static volatile uint32_t core1_decomp_count = 0;
static volatile uint32_t core0_decomp_count = 0;
// When core0 last handed core1 a fragment (DECOMPRESS / ROI_UPDATE). Written by
// core0 only; read by the stall check in raw_hid_pre_receive_kb().
static uint32_t core1_pushed_at = 0;

// The FIFO command core1 last STARTED, and its argument (the Eden job's word; 0 for the
// others). Written by core1 only. When core1 stops answering, this names what it was
// doing: a decode, an Eden keycap, or nothing at all (0 = never got a command).
static volatile uint32_t core1_last_cmd = 0;
static volatile uint32_t core1_last_arg = 0;

static volatile uint8_t core1_buffer[HID_DATA_MAX];
// Despite the "bitlen" name this holds a BYTE count, not a bit count: the number
// of writable bytes from the current dest (get_overlay(idx)+core1_bit_index/8) to
// the end of the 360-byte overlay, i.e. 360 - core1_bit_index/8 (set below). It is
// passed straight as rle_decompress's `max` (bytes). Reused across the DECOMPRESS
// continuation chunks rather than declaring a fresh local each time.
static volatile int16_t core1_max_bitlen;
static volatile uint16_t core1_idx;
static volatile roi_update_data_t core1_roi;
// Set by the dispatcher (core0) before pushing each fragment: whether the overlay being
// staged is the modifier variant currently on screen. core1 only requests a display
// refresh on completion when it is — an off-screen variant (e.g. a Shift image while
// Shift is up) is written to memory but needn't re-render (the modifier-press path picks
// it up). Same publish-before-FIFO-push ordering as core1_idx, so core1 reads the value
// that belongs to the fragment it is completing. See fill_overlay.c overlay_variant_visible.
static volatile bool core1_visible = true;

// Set by core1 once it is inside core1_entry() with IRQs masked; cleared by core0
// before each launch. Read into the boot sub-step paint breadcrumbs (boot_diag.c), so a
// watchdog record from the late-boot window says whether core1 had got that far.
volatile uint32_t g_core1_entered = 0;

typedef enum {
    CORE1_CMD_DECOMPRESS     = 0xcafe0001,
    CORE1_CMD_ROI_UPDATE     = 0xcafe0002,
    CORE1_CMD_RESET_BIT_IDX  = 0xcafe0003,
    CORE1_CMD_EDEN_KEY       = 0xcafe0004,   // followed by ONE argument word
#ifdef POLYKYBD_CRASH_TEST
    CORE1_CMD_CRASH_TEST     = 0xcafe00ff,
#endif
} fifo_command_t;

// Overlay buffers for core1 processing
extern uint8_t overlays [NUM_OVERLAY_SLOTS][72*40/8]; // ResX*ResY/PixelPerByte

// Main function for core1, do not use any prtintf or similar, stack is limited!
// Processes decompression and ROI update commands from core0 via FIFO, handles overlay buffer updates.
// Global variables: core1_bit_index, core1_decomp_count, core1_idx, core1_max_bitlen, core1_buffer, core1_roi
void core1_entry(void) {
    // PRIMASK=1 on core1 — without this core1 hangs whenever overlay/ROI data is processed
    // after the upstream-QMK-master merge (May 2026). The trap is the strongly-overridden
    // ChibiOS Vector80 (SIO_IRQ_PROC1) handler whose CH_IRQ_EPILOGUE triggers an NMI via
    // ICSR.NMIPENDSET, which then runs the ChibiOS context-switch NMI handler on a core
    // with no thread state, hanging it. The FIFO IRQ on RP2040 has known quirks
    // (see pico-sdk issue #284) where it appears to fire despite NVIC->ISER bit being clear.
    // core1 in this codebase has no IRQ-driven work — multicore_fifo_pop_blocking polls
    // FIFO_ST and doesn't need an IRQ to wake — so masking all IRQs here is safe.
    // See keyboards/polykybd/CLAUDE.md for the full investigation.
    __asm volatile("cpsid i" ::: "memory");
    multicore_fifo_drain();
    // Set only after the drain: an EEPROM write waits for an acknowledged PARK only once
    // this reads 1 (wear_leveling_rp2040_flash_quiesce), and a drain after it could
    // swallow that PARK and turn a healthy launch into a forced reset.
    dmb();
    g_core1_entered = 1u;
    dmb();
    while (true) {
        uint32_t cmd = core1_fifo_pop_ram();  // blocks if empty; in RAM (see above)
        core1_last_cmd = cmd;
        core1_last_arg = 0;
        switch (cmd) {
            case CORE1_CMD_DECOMPRESS:{
                    uint16_t data_len = core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX;
                    core1_bit_index += rle_decompress(get_overlay(core1_idx)+core1_bit_index/8, PK_MAX(0,core1_max_bitlen), core1_buffer, data_len, core1_bit_index);

                    if (core1_bit_index >= 360*8 -1) {
                        mark_display_has_overlay_post_upload(core1_idx);
                        // No update_performed() — a host overlay push is not user
                        // activity and must not restart the idle countdown (see
                        // base/update.h). Also keeps core1 out of the idle-timer
                        // state entirely; only core0 writes it now.
                        // Only refresh a variant that is actually on screen (core1_visible).
                        if (core1_visible) {
                            request_disp_refresh();
                        }
                        core1_bit_index = 0;
                    }
                    core1_decomp_count++;
                    if(core1_decomp_count==0) { //handle overflow
                        core1_decomp_count=1;
                    }
                    dmb();
                } break;
            case CORE1_CMD_ROI_UPDATE:{
                    uint8_t data_len = ROI_MAX;
                    if(core1_bit_index==0) {
                        core1_bit_index = core1_roi.y * SCREEN_WIDTH + core1_roi.x;
                        data_len = ROI_START;
                    }
                    core1_bit_index = copy_rectangle_to_overlay(core1_bit_index, get_overlay(core1_idx), core1_buffer, &core1_roi, data_len);
                    if(core1_bit_index >= 2880) {
                        mark_display_has_overlay_post_upload(core1_idx);
                        // No update_performed() — see base/update.h.
                        // Only refresh a variant that is actually on screen (core1_visible).
                        if (core1_visible) {
                            request_disp_refresh();
                        }
                        core1_bit_index = 0;
                    }
                    core1_decomp_count++;
                    if(core1_decomp_count==0) { //handle overflow
                        core1_decomp_count=1;
                    }
                    dmb();
                } break;
            case CORE1_CMD_RESET_BIT_IDX:
                core1_bit_index = 0;
                dmb();
                break;
            case CORE1_CMD_EDEN_KEY:
                // One Eden idle keycap (anim/startup_anim.c). The argument follows the
                // command in the FIFO; the job publishes its own completion. Its stack
                // path is measured: IDLE_STYLES.md, "The idle loop on core1".
                {
                    // The argument is pushed in the same interrupts-off section as
                    // the command (core1_eden_key), so no PARK can sit between them.
                    const uint32_t arg = multicore_fifo_pop_blocking();
                    core1_last_arg = arg;
                    startup_anim_core1_job(arg);
                }
                break;
#ifdef POLYKYBD_CRASH_TEST
            case CORE1_CMD_CRASH_TEST: {
                    // An unaligned word store: ARMv6-M has no unaligned access, so
                    // this HardFaults right here. The vector table is shared with
                    // core0 and PRIMASK does not mask a HardFault, so the naked
                    // HardFault_Handler runs ON CORE1 and the record's `core` field
                    // reads 1. No uprintf -- core1 has no console and a tiny stack.
                    // ⚠️ The address MUST be laundered through a volatile. Written as a
                    // compile-time constant, GCC sees the misalignment and LEGALISES the
                    // store into four strb -- which never fault on ARMv6-M, so core1 wrote
                    // the bytes and carried on and this trigger silently did nothing
                    // (measured 2026-09-04). The volatile hides the alignment, forcing a
                    // real str. Same rule as crash_test.c, different reason from the one
                    // documented there: not deletion, legalisation.
                    static volatile uintptr_t bad_addr;
                    bad_addr = ((uintptr_t)&core1_decomp_count) + 1u;
                    volatile uint32_t *bad = (volatile uint32_t *)bad_addr;
                    // Tag the breadcrumb so the resulting record self-identifies as this
                    // trigger rather than leaving `core=1` as the only evidence.
                    (void)crash_phase_enter(CRASH_PHASE_CORE1_WAIT, 0x00C1);
                    *bad = 0xDEADBEEFu;
                } break;
#endif
            default: break;
        }
    }
}

bool core1_eden_available(void) {
    dmb();
    return g_core1_entered != 0u && !doom_mode_active() && !s_c1_recovering;
}

void core1_eden_key(uint32_t arg) {
    core1_wait_recovered();
    // Both words in one interrupts-off section: an EEPROM write (on the slave's split
    // thread, say) can then never put its PARK between a command and its argument.
    const uint32_t irq = save_and_disable_interrupts();
    multicore_fifo_push_blocking(CORE1_CMD_EDEN_KEY);
    multicore_fifo_push_blocking(arg);
    restore_interrupts(irq);
}

bool core1_is_busy(void) {
    dmb();
    return core0_decomp_count != core1_decomp_count;
}

// ---- core1 stall recovery ----------------------------------------------------
// A fragment takes core1 well under a millisecond, and at most two Eden keycap jobs
// (~4.3 ms each) can sit in front of it. Half a second means core1 has stopped.
#define CORE1_STALL_MS 500u

// What the last recovery found. Printed from the HID path (core1_stall_report()), not
// at recovery time: QMK drops console output nobody drains, and core1 can stop while
// no host is attached (field 2026-10-09: it was already down before the host
// connected). A host report almost always means somebody is reading the console. The
// exception is a host whose console interface was not up yet when it opened the
// device, which it repairs within a second, so the line is printed CORE1_STALL_REPORTS
// times, CORE1_STALL_REPORT_GAP_MS apart. Every copy carries the same recovery ID,
// "recovery <n> since boot at <uptime> ms", which is how the host's problem scan counts
// one recovery once. The uptime is part of the ID because <n> restarts after a reboot.
#define CORE1_STALL_REPORTS      3u
#define CORE1_STALL_REPORT_GAP_MS 10000u
static struct {
    uint32_t count;      // recoveries since boot
    uint32_t stalled_ms; // how long the oldest fragment had waited
    uint32_t last_cmd;   // core1_last_cmd when it was found stopped
    uint32_t last_arg;
    uint32_t c0, c1;     // core0_decomp_count / core1_decomp_count
    uint32_t entered;    // g_core1_entered
    uint32_t at_ms;      // uptime of the recovery: with `count`, the recovery's ID
    uint32_t printed_at; // uptime of the last copy printed
    uint8_t  printed;    // copies printed of this recovery
    bool     relaunched; // the bounded relaunch answered
    const char *where;   // "" (the raw-HID gate), " at suspend", " at EEPROM write"
} s_c1_stall;

// Record what a stalled core1 left behind. Call before the hold clears it.
static void core1_note_stall(uint32_t stalled_ms, const char *where) {
    s_c1_stall.count++;
    s_c1_stall.stalled_ms = stalled_ms;
    s_c1_stall.last_cmd   = core1_last_cmd;
    s_c1_stall.last_arg   = core1_last_arg;
    s_c1_stall.c0         = core0_decomp_count;
    s_c1_stall.c1         = core1_decomp_count;
    s_c1_stall.entered    = g_core1_entered;
    s_c1_stall.at_ms      = timer_read32();
    s_c1_stall.printed    = 0;
    s_c1_stall.where      = where;
    s_c1_stall.relaunched = false;
}

// Hold core1 in reset and drop the work it held. The caller's core1_release() brings
// the service back.
// core1 is in reset: core0 owns every word it shares. Drop the work it held. A fragment
// is lost only when core1 still owed one (`fragment_lost`, read before the reset).
// Otherwise the decode cursor must survive: an image upload spans several reports, and
// a hold between two of them (a suspend, a HID flush) would restart the next at pixel 0.
static void core1_drop_work(bool fragment_lost) {
    // A fragment in flight is lost: its overlay keeps whatever was decoded so far.
    if (fragment_lost) {
        core1_bit_index = 0;
    }
    core1_decomp_count = core0_decomp_count;
    core1_last_cmd     = 0;
    core1_last_arg     = 0;
    g_core1_entered    = 0u;
    // The Eden keycap jobs died with it; without this the idle loop would wait for
    // them and stay on core0 until the next boot.
    startup_anim_core1_lost();
    dmb();
}

static void core1_hold_and_drop(void) {
    const bool fragment_lost = core1_is_busy();
    // Through core1_owner, not a raw PSM write: the hold forces core1 off and waits
    // for the PSM, and its release relaunches the service (bounded, 100 ms) once no
    // other holder remains. A second driver of the PSM here would bypass the count.
    core1_hold();
    core1_drop_work(fragment_lost);
}

// core1 owes a fragment and has not answered for CORE1_STALL_MS: reset it, drop the
// work it lost, and launch the service again.
static void core1_recover(uint32_t stalled_ms) {
    core1_note_stall(stalled_ms, "");
    core1_hold_and_drop();
    // The relaunch is bounded: a core1 that does not come back must not take core0's
    // main loop with it.
    s_c1_stall.relaunched = core1_release();
}

// ---- core1 across a USB suspend (base/core1_suspend.h) -----------------------
// Master only, like the raw-HID gate: on the slave the split thread hands core1 its
// fragments, and a fragment pushed into a held core1 would spin
// core1_decompress_fragment() until the watchdog fires.
static core1_suspend_t s_c1_suspend;
static bool            s_c1_suspend_stalled;   // this suspend's hold recorded a stall

// core1 runs the overlay service on this half, and core0 may reset it from the main
// loop: the master (the slave's split thread pushes its fragments), no DOOM.
static bool core1_service_ours(void) {
    return is_keyboard_master() && !doom_mode_active() && core1_tenant() == CORE1_TENANT_SERVICE;
}

void core1_suspend_tick(void) {
    const bool eligible = core1_service_ours() && !core1_held();
    const bool owes = eligible && (core1_is_busy() || startup_anim_core1_owes());
    const uint32_t now = timer_read32();
    switch (core1_suspend_step(&s_c1_suspend, eligible, owes, now)) {
        case CORE1_SUSPEND_HOLD:
            s_c1_suspend_stalled = false;
            core1_hold_and_drop();
            break;
        case CORE1_SUSPEND_HOLD_STALLED: {
            // A fragment's age is known; an Eden job's is not, so it is at least the
            // time this suspend waited for it.
            uint32_t stalled_ms = s_c1_suspend.waited;
            if (core1_is_busy()) {
                const uint32_t fragment_ms = timer_elapsed32(core1_pushed_at);
                if (fragment_ms > stalled_ms) stalled_ms = fragment_ms;
            }
            core1_note_stall(stalled_ms, " at suspend");
            s_c1_suspend_stalled = true;
            core1_hold_and_drop();
        } break;
        case CORE1_SUSPEND_NONE:
            break;
    }
}

void core1_resume(void) {
    if (!core1_resume_step(&s_c1_suspend)) {
        return;
    }
    const bool relaunched = core1_release();
    if (s_c1_suspend_stalled) {
        s_c1_stall.relaunched = relaunched;
        s_c1_suspend_stalled  = false;
    }
    // A clean hold whose relaunch did not answer is reported by core1_owner, and the
    // raw-HID gate resets that core1 again once a fragment waits on it.
}

// ---- core1 during an EEPROM write ---------------------------------------------
// Strong overrides of the weak hooks in platforms/chibios/drivers/wear_leveling/
// wear_leveling_rp2040_flash.c (UPSTREAM_PATCHES.md), so EVERY EEPROM write is
// covered, on both halves and from any thread: the suspend flush, HID keymap and
// macro edits, the slave's keymap writes on its split thread, KC_STORE_EE, RGB keys.
//
// The quiesce hook runs with core0's interrupts disabled, so no thread can hand core1
// new work until the write ends. It sends CORE1_CMD_PARK and waits for core1 to
// acknowledge it from its RAM wait (core1_fifo_pop_ram): then core1 has finished every
// earlier command and is off the flash bus. No acknowledgement within
// CORE1_FLASH_WAIT_US means core1 stopped inside flash code: it is forced into reset
// for the write, and its lost work is dropped in the same interrupts-off window, so no
// thread can wait on it once interrupts return. Hand-overs then sleep
// (s_c1_recovering) until the resume hook has recorded the stall and relaunched core1
// through core1_owner. DOOM's engine keeps the old behaviour, and a core1 that has not
// yet entered core1_entry() (a launch in progress) is not waited for.
static volatile bool s_c1_forced_off_for_flash;

void wear_leveling_rp2040_flash_quiesce(void) {
    if (core1_held() || doom_mode_active() || core1_tenant() != CORE1_TENANT_SERVICE ||
        g_core1_entered == 0u) {
        return;   // in reset, not the service, or not in core1_entry() yet (unchanged)
    }
    const uint32_t acks   = s_c1_park_acks;
    const uint32_t start  = timer_hw->timerawl;
    bool           pushed = false;
    for (;;) {
        // Never block on a full FIFO here: a core1 that stopped would hold it full.
        if (!pushed && multicore_fifo_wready()) {
            sio_hw->fifo_wr = CORE1_CMD_PARK;
            __asm volatile("sev");
            pushed = true;
        }
        const bool     parked = pushed && s_c1_park_acks != acks;
        const uint32_t waited = timer_hw->timerawl - start;
        switch (core1_flash_wait_step(!parked, waited)) {
            case CORE1_FLASH_PROCEED:
                return;
            case CORE1_FLASH_WAIT:
                break;
            case CORE1_FLASH_FORCE_OFF: {
                const bool fragment_lost = core1_is_busy();
                // A raw PSM write, reconciled with core1_owner's count in the resume
                // hook: core1_hold() there forces it off again and counts the hold.
                core1_hw_force_off();
                core1_note_stall(waited / 1000u, " at EEPROM write");
                core1_drop_work(fragment_lost);
                s_c1_recovering           = true;
                s_c1_forced_off_for_flash = true;
                return;
            }
        }
    }
}

void wear_leveling_rp2040_flash_resume(void) {
    if (!s_c1_forced_off_for_flash) {
        return;
    }
    s_c1_forced_off_for_flash = false;
    core1_hold();   // the work was dropped in the quiesce window; this counts the hold
    s_c1_stall.relaunched = core1_release();
    s_c1_recovering       = false;
}

bool core1_suspend_flush_ready(void) {
    if (core1_held()) {
        return true;    // in reset: nothing to fetch
    }
    if (core1_service_ours()) {
        return false;   // master: core1_suspend_tick() takes the hold first
    }
    if (doom_mode_active() || core1_tenant() != CORE1_TENANT_SERVICE) {
        return true;    // DOOM's engine: unchanged behaviour
    }
    // Slave: its split thread hands core1 fragments, so the main loop must not reset
    // it. Flush once core1 owes nothing: the Eden jobs poly_suspend() left in flight
    // take a few ms, and an idle core1 waits in RAM (core1_fifo_pop_ram). A core1 that never finishes skips this suspend's flush; the dirty
    // flags stay set for the next one.
    return !core1_is_busy() && !startup_anim_core1_owes();
}

void core1_off_during(void (*write)(void)) {
    if (!core1_service_ours() || core1_held()) {
        write();   // core1 is off already, or not ours to reset (unchanged behaviour)
        return;
    }
    // The HID path takes a command only when core1 owes no fragment (the raw-HID gate),
    // so what the reset can drop here is Eden keycap jobs, which the hold marks done.
    core1_hold_and_drop();
    write();
    (void)core1_release();   // a relaunch that does not answer is reported by core1_owner
}

void core1_stall_report(void) {
    if (s_c1_stall.count == 0 || s_c1_stall.printed >= CORE1_STALL_REPORTS) {
        return;
    }
    // Recorded at suspend but not relaunched yet: the wake decides the outcome.
    if (s_c1_suspend_stalled) {
        return;
    }
    if (s_c1_stall.printed > 0 && timer_elapsed32(s_c1_stall.printed_at) < CORE1_STALL_REPORT_GAP_MS) {
        return;
    }
    s_c1_stall.printed++;
    s_c1_stall.printed_at = timer_read32();
    // `where` (" at suspend" / " at EEPROM write") sits before the outcome on purpose: the host's problem scan
    // (PolyKybdHost problem_scan.py) reads "core1 relaunched (recovery N since boot at
    // M ms" as one unbroken run.
    uprintf("WARNING core1 stalled: no answer for %lu ms%s (last cmd 0x%08lx arg 0x%08lx, "
            "counts %lu/%lu, entered %lu) - %s (recovery %lu since boot at %lu ms, report %u/%u)\n",
            (unsigned long)s_c1_stall.stalled_ms, s_c1_stall.where ? s_c1_stall.where : "",
            (unsigned long)s_c1_stall.last_cmd,
            (unsigned long)s_c1_stall.last_arg, (unsigned long)s_c1_stall.c0,
            (unsigned long)s_c1_stall.c1, (unsigned long)s_c1_stall.entered,
            s_c1_stall.relaunched ? "core1 relaunched" : "core1 relaunch FAILED, overlays degraded until reboot",
            (unsigned long)s_c1_stall.count, (unsigned long)s_c1_stall.at_ms,
            (unsigned)s_c1_stall.printed, (unsigned)CORE1_STALL_REPORTS);
}

// Strong override of the weak hook in tmk_core/protocol/chibios/usb_main.c:
// when core1 is still chewing on the previous fragment, refuse to pull the
// next packet off the Raw HID OUT queue this main-loop pass. The packet stays
// queued by the USB driver (RAW_OUT_CAPACITY=4) and matrix_task gets to run.
//
// ⚠️ The refusal must END. A core1 that has stopped never catches up, and a gate
// that waits for it closes the raw HID OUT queue for good: the four slots fill and
// the endpoint NAKs every later write, while typing, the console and the idle
// animations go on, and no watchdog fires because nothing spins. Field 2026-10-09:
// the host's writes timed out with 0x3E5 for a minute until a replug. So after
// CORE1_STALL_MS the gate relaunches core1 instead of waiting for it.
//
// Master only: there the gate is the one place a fragment waits, and every core1
// user runs on the main thread. On the slave the split thread hands core1 its
// fragments, and a relaunch from the main thread could interleave with that push.
bool raw_hid_pre_receive_kb(void) {
    // A host that sends reports is awake. The wake normally released the suspend hold
    // already (suspend_wakeup_init_kb()); this covers a wake event QMK's USB event
    // queue dropped, which would otherwise leave core1 held and this gate refusing
    // every report once a fragment waits. A no-op when no suspend hold is taken.
    core1_resume();
    if (!core1_is_busy()) {
        return true;
    }
    if (!is_keyboard_master() || doom_mode_active() || core1_held()) {
        return false;   // core1 is busy on purpose (a flash write holds it), or not ours to reset from here
    }
    const uint32_t stalled_ms = timer_elapsed32(core1_pushed_at);
    if (stalled_ms < CORE1_STALL_MS) {
        return false;
    }
    core1_recover(stalled_ms);
    return true;
}

void core1_decompress_fragment(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* compressed, bool visible, bool first) {
    // Defense in depth: callers that respect the raw_hid_pre_receive_kb() gate
    // will never enter the wait. For any caller that didn't gate (e.g. the
    // split-sync bridge path), spin without the uprintf — the previous wait
    // body was burning core0 cycles in a format-and-sink path that starved
    // matrix scan during back-pressure.
    core1_wait_recovered();
    dmb();
    // Tagged so a watchdog timeout in this spin reads as "core0 waiting on core1"
    // (the core1-hang class, see CLAUDE.md) rather than as an anonymous hang.
    uint32_t crash_tag = crash_phase_enter(CRASH_PHASE_CORE1_WAIT, 0);
    while(core0_decomp_count!=core1_decomp_count) {
        dmb();
    }
    crash_phase_leave(crash_tag);
    // A new image starts at pixel 0. core1 only rewinds its cursor when it has decoded
    // a whole image, so an image left incomplete (the fragment a stall recovery
    // dropped) would otherwise start the NEXT key's image part-way through its buffer.
    // core1 is idle on decode work here (the counts match), so core0 may write it.
    if (first) {
        core1_bit_index = 0;
    }
    //copy data to dedicated buffers
    uint8_t data_len = core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX;
    core1_max_bitlen = 360 - core1_bit_index/8;
    core1_idx = overlay_idx;
    core1_visible = visible;
    for(uint8_t i=0;i<data_len;++i) {
        core1_buffer[i] = compressed[i]; //memcopy not avialable for volatile memory
    }

#ifdef POLY_DEBUG_HID
#    ifdef CORE1_STACK_HWM
    uprintf("CORE1: Key 0x%x (mod 0x%x) fragment decompression: (added %d bytes, bit index: %d, stack HWM: %lu).\n", keycode, mod, core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX, core1_bit_index, (unsigned long)core1_stack_high_water_mark());
#    else
    uprintf("CORE1: Key 0x%x (mod 0x%x) fragment decompression: (added %d bytes, bit index: %d).\n", keycode, mod, core1_bit_index==0?COMPRESSED_START:COMPRESSED_MAX, core1_bit_index);
#    endif
#else
    (void)keycode;
    (void)mod;
#endif
    core1_pushed_at = timer_read32();
    core0_decomp_count++;
    if(core0_decomp_count==0) { //handle overflow
        core0_decomp_count=1;
    }
    dmb();
    //allow core1 to start decompressing
    multicore_fifo_push_blocking(CORE1_CMD_DECOMPRESS);
}

void core1_roi_start(void) {
    // No wait or dmb needed: RESET touches only core1_bit_index (not the shared buffers),
    // FIFO ordering guarantees any in-flight DECOMPRESS/ROI_UPDATE completes atomically
    // before this RESET runs, and the immediately-following core1_update_roi() performs
    // its own decomp-count wait before writing buffers.
    core1_wait_recovered();
    multicore_fifo_push_blocking(CORE1_CMD_RESET_BIT_IDX);
}

void core1_update_roi(uint8_t keycode, uint8_t mod, uint16_t overlay_idx, const uint8_t* data, const roi_update_data_t* roi, bool visible) {
    // See core1_decompress_fragment for the backpressure rationale.
    core1_wait_recovered();
    dmb();
    // Tagged so a watchdog timeout in this spin reads as "core0 waiting on core1"
    // (the core1-hang class, see CLAUDE.md) rather than as an anonymous hang.
    uint32_t crash_tag = crash_phase_enter(CRASH_PHASE_CORE1_WAIT, 0);
    while(core0_decomp_count!=core1_decomp_count) {
        dmb();
    }
    crash_phase_leave(crash_tag);
    //copy data to dedicated buffers
    //core1_max_bitlen = 360 - core1_bit_index/8;
    core1_roi = *roi;
    core1_idx = overlay_idx;
    core1_visible = visible;
    const uint8_t data_len = core1_bit_index==0?ROI_START:ROI_MAX;
    for(uint8_t i=0;i<data_len;++i) {
        core1_buffer[i] =  data[i]; //memcopy not available for volatile memory
    }

#ifdef POLY_DEBUG_HID
#    ifdef CORE1_STACK_HWM
    uprintf("CORE1: Key 0x%x (mod 0x%x) roi update: (added %d bytes, bit index: %d, stack HWM: %lu).\n", keycode, mod, data_len, core1_bit_index, (unsigned long)core1_stack_high_water_mark());
#    else
    uprintf("CORE1: Key 0x%x (mod 0x%x) roi update: (added %d bytes, bit index: %d).\n", keycode, mod, data_len, core1_bit_index);
#    endif
#else
    (void)keycode;
    (void)mod;
#endif
    core1_pushed_at = timer_read32();
    core0_decomp_count++;
    if(core0_decomp_count==0) { //handle overflow
        core0_decomp_count=1;
    }
    dmb();
    //allow core1 to start decompressing
    multicore_fifo_push_blocking(CORE1_CMD_ROI_UPDATE);
}
#ifdef POLYKYBD_CRASH_TEST
void core1_crash_test(void) {
    multicore_fifo_push_blocking(CORE1_CMD_CRASH_TEST);
}
#endif


#else  // !USE_CORE1: no core1 service, so the Eden idle loop renders on core0
bool core1_eden_available(void) { return false; }
void core1_eden_key(uint32_t arg) { (void)arg; }
void core1_suspend_tick(void) {}
void core1_resume(void) {}
bool core1_suspend_flush_ready(void) { return true; }
void core1_off_during(void (*write)(void)) { write(); }
#endif
