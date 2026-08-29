#include "polymod_core1.h"
#include "polymod_core1_irq.h"

#include "hardware/structs/scb.h"
#include "hardware/timer.h"   // time_us_64 (bounded launch deadline)

#include <stdbool.h>
#include <stdint.h>

#define CORE1_STACK_SIZE 384

static uint32_t core1_stack[CORE1_STACK_SIZE/4]
    __attribute__((aligned(8)));

// Diagnostic counters for the BOUNDED runtime relaunch (doom session teardown
// and the fw_staging FONTPACK/doom-flash restart both funnel through
// multicore_launch_core1_bounded). A non-zero timeout count means a relaunch
// left core1 desynced — the suspected trigger of the intermittent slave wedge on
// a post-doom reflash (FW-9). Surfaced to the master via fw_staging's status RPC
// so the rig (master console only) can read them; see fw_up_log_slave_status.
static uint16_t s_bounded_launch_calls    = 0;
static uint16_t s_bounded_launch_timeouts = 0;

void multicore_launch_core1_bounded_stats(uint16_t *calls, uint16_t *timeouts) {
    if (calls)    *calls    = s_bounded_launch_calls;
    if (timeouts) *timeouts = s_bounded_launch_timeouts;
}

#ifdef CORE1_STACK_HWM
// Stack high-water-mark instrumentation. Enable by adding CORE1_STACK_HWM to
// rules.mk (OPT_DEFS += -DCORE1_STACK_HWM). See readme.md "For developers".
#define CORE1_STACK_SENTINEL 0xDEADBEEFu

// Walks core1_stack from the low address upward and returns the number of bytes
// that have been written (i.e. no longer hold the sentinel). The deepest the stack
// ever reached during execution. Safe to call from core0 while core1 is running —
// reads are racy w.r.t. transient writes but the high-water mark is monotonic so
// at worst we under-report by one frame.
uint32_t core1_stack_high_water_mark(void) {
    const size_t total = CORE1_STACK_SIZE / sizeof(uint32_t);
    size_t untouched = 0;
    while (untouched < total && core1_stack[untouched] == CORE1_STACK_SENTINEL) {
        untouched++;
    }
    return (total - untouched) * sizeof(uint32_t);
}
#endif

static void __attribute__ ((naked)) core1_trampoline(void) {
    // Mask IRQs on core1 as its VERY FIRST instruction, before core1_wrapper or
    // core1_entry run. core1_entry() also does `cpsid i`, but several instructions
    // (this trampoline + core1_wrapper's stack-guard install) execute on core1 with
    // IRQs still enabled before it gets there. In that window the strongly-overridden
    // ChibiOS Vector80 (SIO_IRQ_PROC1) can fire — its CH_IRQ_EPILOGUE triggers an NMI
    // via ICSR.NMIPENDSET, which runs the context-switch NMI handler on a core with no
    // thread state and hangs it (see keyboards/polykybd/base/fw_staging.c and CLAUDE.md).
    // On a cold power-on that window is usually harmless, but a watchdog-reset boot (the
    // firmware-apply / QK_REBOOT reset path) can inherit SIO-FIFO/IRQ state a power-on
    // doesn't, so a stale/pending FIFO IRQ fires here and hangs the MASTER on the boot
    // splash — core0's launch handshake (multicore_fifo_pop_blocking) then waits forever
    // for a core1 that is stuck in the NMI (field: "stuck on the PolyKybd splash after
    // flashing / reset"). Masking here closes the window: core1 has no IRQ-driven work
    // (it polls FIFO_ST), so keeping IRQs masked for its whole lifetime is safe.
    __asm volatile ("cpsid i\n\tpop {r0, r1, pc}");
}

void multicore_launch_core1_raw(void (*entry)(void), uint32_t *sp, uint32_t vector_table) {
    // Allow for the fact that the caller may have already enabled the FIFO IRQ for their
    // own purposes (expecting FIFO content after core 1 is launched). We must disable
    // the IRQ during the handshake, then restore afterwards.
    uint irq_num = SIO_FIFO_IRQ_NUM(0);
    bool enabled = irq_is_enabled(irq_num);
    irq_set_enabled(irq_num, false);

    // Values to be sent in order over the FIFO from core 0 to core 1
    //
    // vector_table is value for VTOR register
    // sp is initial stack pointer (SP)
    // entry is the initial program counter (PC) (don't forget to set the thumb bit!)
    const uint32_t cmd_sequence[] =
            {0, 0, 1, (uintptr_t) vector_table, (uintptr_t) sp, (uintptr_t) entry};

    uint seq = 0;
    do {
        uint cmd = cmd_sequence[seq];
        // Always drain the READ FIFO (from core 1) before sending a 0
        if (!cmd) {
            multicore_fifo_drain();
            // Execute a SEV as core 1 may be waiting for FIFO space via WFE
            SEV();
        }
        multicore_fifo_push_blocking(cmd);
        uint32_t response = multicore_fifo_pop_blocking();
        // Move to next state on correct response (echo-d value) otherwise start over
        seq = cmd == response ? seq + 1 : 0;
    } while (seq < count_of(cmd_sequence));

    irq_set_enabled(irq_num, enabled);
}

int core1_wrapper(int (*entry)(void), void *stack_base) {
#if INSTALL_STACK_GUARD
    // install core1 stack guard
    runtime_init_per_core_install_stack_guard(stack_base);
#else
    (void)stack_base;
#endif
    //runtime_run_per_core_initializers();
    return (*entry)();
}

void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes) {
#ifdef CORE1_STACK_HWM
    // Paint the stack with a sentinel so core1_stack_high_water_mark() can later
    // report the deepest point reached. Must happen before the trampoline values
    // are written to the top three slots below.
    for (size_t i = 0; i < stack_size_bytes / sizeof(uint32_t); i++) {
        stack_bottom[i] = CORE1_STACK_SENTINEL;
    }
#endif
    uint32_t *stack_ptr = stack_bottom + stack_size_bytes / sizeof(uint32_t);
    // Push values onto top of stack for core1_trampoline

    stack_ptr -= 3;
    uint32_t vector_table = scb_hw->vtor;
    stack_ptr[0] = (uintptr_t) entry;
    stack_ptr[1] = (uintptr_t) stack_bottom;
    stack_ptr[2] = (uintptr_t) core1_wrapper;

    multicore_launch_core1_raw(core1_trampoline, stack_ptr, vector_table);
}

void multicore_launch_core1(void) {
    multicore_launch_core1_with_stack(core1_entry, core1_stack, CORE1_STACK_SIZE);
}

// Bounded variant of the launch handshake (see core1.h). Identical protocol
// to multicore_launch_core1_raw, but every FIFO wait checks an overall
// deadline — if core1 is not in the bootrom wait loop (held in reset, or
// mid power-up after a PSM force-off), the unbounded handshake blocks
// forever (fw_staging.c carries the same lore for the post-apply reboot).
bool multicore_launch_core1_bounded(uint32_t total_timeout_us) {
    uint32_t *stack_bottom = core1_stack;
    uint32_t *stack_ptr    = stack_bottom + CORE1_STACK_SIZE / sizeof(uint32_t);
    stack_ptr -= 3;
    stack_ptr[0] = (uintptr_t) core1_entry;
    stack_ptr[1] = (uintptr_t) stack_bottom;
    stack_ptr[2] = (uintptr_t) core1_wrapper;

    uint irq_num = SIO_FIFO_IRQ_NUM(0);
    bool enabled = irq_is_enabled(irq_num);
    irq_set_enabled(irq_num, false);

    const uint32_t cmd_sequence[] =
            {0, 0, 1, (uintptr_t) scb_hw->vtor, (uintptr_t) stack_ptr, (uintptr_t) core1_trampoline};

    if (s_bounded_launch_calls != UINT16_MAX) s_bounded_launch_calls++;
    const uint64_t deadline = time_us_64() + total_timeout_us;
    bool ok  = true;
    uint seq = 0;
    do {
        uint cmd = cmd_sequence[seq];
        if (!cmd) {
            multicore_fifo_drain();
            SEV();
        }
        while (!multicore_fifo_wready()) {
            if (time_us_64() > deadline) { ok = false; break; }
            tight_loop_contents();
        }
        if (!ok) break;
        sio_hw->fifo_wr = cmd;
        SEV();
        while (!multicore_fifo_rvalid()) {
            if (time_us_64() > deadline) { ok = false; break; }
            tight_loop_contents();
        }
        if (!ok) break;
        uint32_t response = sio_hw->fifo_rd;
        seq = cmd == response ? seq + 1 : 0;
    } while (seq < count_of(cmd_sequence));

    irq_set_enabled(irq_num, enabled);
    if (!ok && s_bounded_launch_timeouts != UINT16_MAX) s_bounded_launch_timeouts++;
    return ok;
}
