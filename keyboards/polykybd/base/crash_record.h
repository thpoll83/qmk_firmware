// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Crash diagnostics: what a HardFault, an unhandled exception or a watchdog
// timeout leaves behind, and how the next boot reports it.
//
// The Cortex-M0+ has ONE fault vector (HardFault) and no fault-status or
// fault-address registers, so the only evidence a fault leaves is the stacked
// exception frame: r0-r3, r12, LR, the faulting PC and xPSR. Before this module
// the firmware's HardFault vector was ChibiOS's weak `b .` loop -- a fault on
// either core was an instant, silent lockup with no trace anywhere, which is how
// the HID-apply brick (qmk#258) cost ten probe rounds.
//
// Three RP2040 facts this design rests on:
//   1. SRAM survives a watchdog / soft reset, not a power cycle. So the crash
//      record lives in a NOLOAD RAM block (`.ram0.crash_record`, the same trick
//      the double-tap bootloader uses for its magic word), guarded by a magic +
//      CRC because after a power-on the block is random.
//   2. The reset cause is readable: VREG_AND_CHIP_RESET.CHIP_RESET says POR vs
//      RUN-pin vs PSM restart, WATCHDOG.REASON says timer vs forced. Nothing in
//      the firmware read either before this.
//   3. A watchdog reset resets the whole chip cleanly (it is what mcu_reset()
//      already uses), so the fault handler can record and REBOOT instead of
//      hanging -- the board comes back on its own, with the record intact.
//
// Lifecycle:
//   fault / halt / timeout  -> record into the NOLOAD block (+ watchdog reboot)
//   next boot, crash_record_init()
//                           -> validate the block, read the reset cause,
//                              archive the record to one flash sector below the
//                              apply log (survives a later power cycle / BOOTSEL
//                              recovery), clear the RAM copy, remember it as
//                              "fresh" for THIS boot
//   boot banner             -> `   crash: side=master kind=...` once per crash,
//                              re-emitted with the banner for a late console
//   HID cmd 39              -> read the archived record (master or the last
//                              pulled slave one) / clear the archive
//   slave                   -> the master pulls the slave's record over
//                              USER_SYNC_SLAVE_DATA once per link-up and prints
//                              it as `side=slave`; the slave has no console.
//
// The PHASE breadcrumb is the piece that makes a HANG diagnosable: a watchdog
// timeout leaves no exception frame, so the loop tags what it is doing (which HID
// command, which split transaction, a core1 wait, a flash program) into the same
// NOLOAD block, and the timeout record carries the last tag.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- the record -----------------------------------------------------------

#define CRASH_RECORD_MAGIC 0xC4A5C0DEu

enum crash_kind {
    CRASH_KIND_NONE      = 0,
    CRASH_KIND_HARDFAULT = 1,  // HardFault vector: frame captured from the active stack
    CRASH_KIND_UNHANDLED = 2,  // any other unhandled vector (ICSR names it)
    CRASH_KIND_WATCHDOG  = 3,  // synthesised at boot from WATCHDOG.REASON.TIMER
    CRASH_KIND_HALT      = 4,  // crash_record_halt(): a deliberate "cannot continue"
};

// Reset-cause bits captured at the boot that reported the record.
#define CRASH_RESET_HAD_POR     (1u << 0)
#define CRASH_RESET_HAD_RUN     (1u << 1)
#define CRASH_RESET_HAD_PSM     (1u << 2)
#define CRASH_RESET_WD_TIMER    (1u << 4)
#define CRASH_RESET_WD_FORCE    (1u << 5)

#define CRASH_FW_LEN 8

// 48 bytes, packed: fits a HID report beside its header and the slave->master
// pull beside its flags byte (RPC_S2M_BUFFER_SIZE 72). Field order is the wire
// order -- PolyKybdHost decodes it with a struct format, so append, never insert.
typedef struct __attribute__((packed)) {
    uint32_t magic;         // CRASH_RECORD_MAGIC
    uint8_t  kind;          // enum crash_kind
    uint8_t  core;          // SIO CPUID of the core that recorded it
    uint8_t  consecutive;   // 1 = first crash after a clean boot, 2 = the boot after that crashed too, ...
    uint8_t  reset_reason;  // CRASH_RESET_* of the boot that reported it (0 while still in RAM)
    uint32_t pc;            // stacked return address (the faulting instruction, or its successor)
    uint32_t lr;
    uint32_t sp;            // the stack pointer the frame was read from
    uint32_t xpsr;
    uint32_t icsr;          // SCB->ICSR: VECTACTIVE names the exception
    uint32_t uptime_ms;     // timer_read32() at the crash (0 for a watchdog record)
    uint16_t phase;         // enum crash_phase -- what the loop was doing
    uint16_t phase_arg;     // per-phase detail (HID cmd id, split tid, ...)
    char     fw[CRASH_FW_LEN];  // FW_VERSION, NUL-padded: names the ELF to addr2line against
    uint32_t crc;           // crc32 over everything above
} poly_crash_record_t;

_Static_assert(sizeof(poly_crash_record_t) == 48, "poly_crash_record_t is a wire format");

// --- the phase breadcrumb -------------------------------------------------

enum crash_phase {
    CRASH_PHASE_UNKNOWN    = 0,
    CRASH_PHASE_BOOT       = 1,   // post_init not finished
    CRASH_PHASE_LOOP       = 2,   // main loop, nothing tagged
    CRASH_PHASE_HID        = 3,   // raw_hid_receive(), arg = command id
    CRASH_PHASE_BRIDGE     = 4,   // send_to_bridge(), arg = transaction id
    CRASH_PHASE_CORE1_WAIT = 5,   // core0 spinning on core1's counter
    CRASH_PHASE_FLASH      = 6,   // flash program/erase, arg = 1 program / 2 erase
    CRASH_PHASE_SUSPEND    = 7,   // USB suspend loop
    CRASH_PHASE_APPLY      = 8,   // firmware self-apply (watchdog is OFF here)
};
// PolyKybdHost's crash_report.py names these for the dialog -- keep the two in step.

// Tag what the loop is doing. Returns the previous tag so a nested site can
// restore it: `uint32_t p = crash_phase_enter(...); ...; crash_phase_leave(p);`
// Two stores into NOLOAD RAM, cheap enough for a per-HID-report site.
uint32_t crash_phase_enter(uint16_t phase, uint16_t arg);
void     crash_phase_leave(uint32_t prev);

// --- boot-time capture and reporting --------------------------------------

// Call ONCE at the very top of keyboard_pre_init_user() -- before any PolyKybd
// init and before QMK's own (matrix, split link, status OLED all run between the
// two hooks), so a fault anywhere in this boot is tagged phase BOOT and the
// previous record is captured before it can recur. Reads the reset cause,
// validates the NOLOAD block, captures a fresh record into RAM, clears the
// NOLOAD copy and writes the record to the flash archive right there -- a copy
// parked in RAM until post_init would be lost to a reset in the boot window,
// and a fault that recurs there every boot would then never be archived.
// The write runs WITHOUT the fw_staging core1 lockout, and that is only
// correct because core1 has never been launched at this point (it is parked in
// the bootrom, fetching nothing from XIP): the lockout's release does a bounded
// RELAUNCH of core1, which would leave post_init's unbounded
// multicore_launch_core1() handshake blocked forever. So this must stay BEFORE
// that launch; never move it into post_init.
void crash_record_init(void);

// True when THIS boot followed a crash (a record was captured by init).
bool crash_record_fresh(void);

// The last archived record (this boot's, or an older one read back from flash).
// Returns false when the archive is empty.
bool crash_record_archived(poly_crash_record_t *out);

// Erase the flash archive and forget the fresh flag + the pulled slave copy.
void crash_record_clear(void);

// One console line describing `rec` for `side` ("master" / "slave"), the exact
// shape PolyKybdHost and the HIL rig parse:
//   crash: side=master kind=hardfault core=0 pc=0x10012345 lr=... sp=... psr=... icsr=...
//          phase=3:0x0015 up=123456ms n=1 reason=0x22 fw=0.18.0
// Returns the number of characters written (snprintf semantics).
int crash_record_format(const poly_crash_record_t *rec, const char *side, char *buf, size_t n);

// Print this boot's fresh master record and the pulled slave record (each once
// per call, only if present). Called from the boot banner and its re-emit tick.
void crash_record_emit_lines(void);

// Fill a HID cmd-39 reply body: [0] flags, [1..48] record (zeroed when absent).
//   which: 0 = the master's archive, 1 = the last slave record pulled.
//   flags: bit0 present, bit1 fresh (recorded by the boot before this one).
// Returns the number of bytes written (49).
#define CRASH_HID_FLAG_PRESENT (1u << 0)
#define CRASH_HID_FLAG_FRESH   (1u << 1)
#define CRASH_HID_BODY_LEN     (1u + sizeof(poly_crash_record_t))
uint8_t crash_record_hid_body(uint8_t which, uint8_t *out, uint8_t out_len);

// --- the split link ---------------------------------------------------------

// Slave side: answer the master's USER_SYNC_SLAVE_DATA pull (kind
// SLAVE_DATA_CRASH). Same body as the HID reply for `which` 0.
void crash_record_slave_reply(uint8_t *out, uint8_t out_len);

// Master side: called by slave_data.c with the body the slave answered. Stores
// it as the "slave" record and prints its console line once when it is fresh.
// Returns false when the body did not decode (short / bad CRC).
bool crash_record_note_slave(const uint8_t *body, uint8_t len);

// --- the watchdog -----------------------------------------------------------

// Arm the hardware watchdog. Call at the END of keyboard_post_init_user() -- the
// one-time boot work (a dynamic-keymap discard can block for seconds) runs
// before it. From then on the main loop must feed it every CRASH_WATCHDOG_MS.
#define CRASH_WATCHDOG_MS 8000u   // the RP2040 maximum is ~8.3 s
void crash_watchdog_start(void);
void crash_watchdog_feed(void);
// Disarm before anything that legitimately stops the loop for longer than the
// timeout and never returns: the firmware self-apply, a bootloader jump, a
// deliberate reset. Leaving it armed across a BOOTSEL entry would reboot the
// bootrom out from under a UF2 copy.
void crash_watchdog_stop(void);

// Record a deliberate "cannot continue" (kind HALT) and reboot. Never returns.
void crash_record_halt(uint16_t arg) __attribute__((noreturn));
