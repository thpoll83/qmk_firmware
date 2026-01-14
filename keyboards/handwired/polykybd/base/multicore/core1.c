#include "core1.h"
#include "irq.h"

#include "hardware/structs/scb.h"

#define CORE1_STACK_SIZE 256

static uint32_t core1_stack[CORE1_STACK_SIZE/4]
    __attribute__((aligned(8)));

static void __attribute__ ((naked)) core1_trampoline(void) {
    __asm volatile ("pop {r0, r1, pc}");
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
