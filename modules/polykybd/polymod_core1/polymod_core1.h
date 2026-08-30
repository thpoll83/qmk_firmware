#pragma once

#include "polymod_core1_fifo.h"

#include <stddef.h>

#define SIO_IRQ_PROC0 15
#define SIO_FIFO_IRQ_NUM(core) (SIO_IRQ_PROC0 + (core))

void multicore_launch_core1(void);

// Like multicore_launch_core1(), but the FIFO handshake is bounded by an
// overall deadline instead of blocking forever. False = core1 never answered
// (not in the bootrom wait loop) — the caller can PSM-reset it and retry.
// The unbounded launcher stays for the boot path (a boot-time failure has no
// meaningful fallback); this one is for RUNTIME relaunches (doom session
// teardown), where a wedged handshake means a dead keyboard.
bool multicore_launch_core1_bounded(uint32_t total_timeout_us);

// Diagnostic counters for the bounded relaunch: how many were attempted and how
// many timed out (left core1 desynced). Read by fw_staging's status RPC so the
// master console (the rig's only window) can see whether a post-doom reflash
// wedge correlates with a relaunch timeout. Either pointer may be NULL.
void multicore_launch_core1_bounded_stats(uint16_t *calls, uint16_t *timeouts);

// Hold core1 in PSM reset for a flash operation and WAIT for the reset to
// actually latch before returning. A bare FRCE_OFF write returns before the core
// has stopped, so a caller that then erases/programs flash (XIP disappears) can
// fault a still-running core1 and take core0 down with it — the intermittent
// post-doom slave wedge (FW-9): once the doom engine has run on core1, the next
// big erase wedged the slave partway through. Spinning until the FRCE_OFF bit
// reads back set is the proven guard the Doom engine teardown already used.
void multicore_hold_core1_off(void);

// Release the PSM reset taken by multicore_hold_core1_off(). Does NOT relaunch
// core1 — pair with multicore_launch_core1_bounded() to restart the RLE service.
void multicore_release_core1_off(void);

// Launch core1 with a caller-provided entry + stack (the underlying primitive
// of multicore_launch_core1; used by the Doom easter egg to hand core1 to the
// game with a pool-backed stack).
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes);

void core1_entry(void);

#ifdef CORE1_STACK_HWM
uint32_t core1_stack_high_water_mark(void);
#endif

static inline void dmb(void) {
    __asm volatile("dmb" ::: "memory");
}
