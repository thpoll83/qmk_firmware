// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include "serial_usart.h"
#include "serial_protocol.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "wait.h"
#include "debug.h"
#ifdef POLY_HANDSHAKE_DIAG
#    include "hardware/gpio.h"
#    include "hardware/structs/timer.h"
#    include "hardware/structs/iobank0.h"
#    include "hardware/structs/padsbank0.h"
#endif
#ifdef POLY_SLAVE_STAGE_PROBE
// Implemented in keyboards/polykybd/poly_util.c. Lights a keycap per stage on the
// slave so a frozen slave can report where its echo transmit wedges. All call sites
// below are OUTSIDE osalSysLock regions (the probe drives SPI + shift registers).
extern void poly_slave_stage_probe(uint8_t stage);
#endif

#if !defined(MCU_RP)
#    error PIO Driver is only available for Raspberry Pi 2040 MCUs!
#endif

static inline bool receive_impl(uint8_t* destination, const size_t size, sysinterval_t timeout);
static inline bool send_impl(const uint8_t* source, const size_t size);
static inline void pio_serve_interrupt(void);

#define MSG_PIO_ERROR ((msg_t)(-3))

#if defined(POLY_RX_POLL_FIX) && !defined(POLY_RX_POLL_US)
// Pre-poll window (us) before falling back to the IRQ-suspend in sync_rx. The slave echo
// lands ~150 us after the master sends the id; 1500 us is generous headroom while still
// far below the 20 ms receive timeout, so a dead link only costs this window extra.
#    define POLY_RX_POLL_US 1500u
#endif

#if defined(SERIAL_PIO_USE_PIO1)
static const PIO pio = pio1;

OSAL_IRQ_HANDLER(RP_PIO1_IRQ_0_HANDLER) {
    OSAL_IRQ_PROLOGUE();
    pio_serve_interrupt();
    OSAL_IRQ_EPILOGUE();
}
#else
static const PIO pio = pio0;

OSAL_IRQ_HANDLER(RP_PIO0_IRQ_0_HANDLER) {
    OSAL_IRQ_PROLOGUE();
    pio_serve_interrupt();
    OSAL_IRQ_EPILOGUE();
}
#endif

#define UART_TX_WRAP_TARGET 0
#define UART_TX_WRAP 3

// clang-format off
#if defined(SERIAL_USART_FULL_DUPLEX)
static const uint16_t uart_tx_program_instructions[] = {
            //     .wrap_target
    0x9fa0, //  0: pull   block           side 1 [7]
    0xf727, //  1: set    x, 7            side 0 [7]
    0x6001, //  2: out    pins, 1
    0x0642, //  3: jmp    x--, 2                 [6]
            //     .wrap
};
#else
static const uint16_t uart_tx_program_instructions[] = {
            //     .wrap_target
    0x9fa0, //  0: pull   block           side 1 [7]
    0xf727, //  1: set    x, 7            side 0 [7]
    0x6081, //  2: out    pindirs, 1
    0x0642, //  3: jmp    x--, 2                 [6]
            //     .wrap
};
#endif
// clang-format on

static const pio_program_t uart_tx_program = {
    .instructions = uart_tx_program_instructions,
    .length       = 4,
    .origin       = -1,
};

#define UART_RX_WRAP_TARGET 0
#define UART_RX_WRAP 8

// clang-format off
static const uint16_t uart_rx_program_instructions[] = {
            //     .wrap_target
    0x2020, //  0: wait   0 pin, 0
    0xea27, //  1: set    x, 7                   [10]
    0x4001, //  2: in     pins, 1
    0x0642, //  3: jmp    x--, 2                 [6]
    0x00c8, //  4: jmp    pin, 8
    0xc020, //  5: irq    wait 0
    0x20a0, //  6: wait   1 pin, 0
    0x0000, //  7: jmp    0
    0x8020, //  8: push   block
            //     .wrap
};
// clang-format on

static const pio_program_t uart_rx_program = {
    .instructions = uart_rx_program_instructions,
    .length       = 9,
    .origin       = -1,
};

thread_reference_t rx_thread        = NULL;
static int         rx_state_machine = -1;

thread_reference_t tx_thread        = NULL;
static int         tx_state_machine = -1;

#ifdef POLY_HANDSHAKE_DIAG
volatile uint32_t g_rx_framing_errors = 0;   // RX-SM framing/break errors (bad stop bit)
volatile uint32_t g_pio_irq_entries   = 0;   // # times the PIO1 IRQ handler actually ran
volatile uint32_t g_pio_irq_rxne      = 0;   // # times it took the rx-not-empty branch
volatile uint32_t g_poll_hits         = 0;   // # sync_rx polls that caught a byte in the window
volatile uint32_t g_poll_miss         = 0;   // # sync_rx polls whose window expired empty
volatile uint32_t g_poll_max_us       = 0;   // worst observed poll latency (us) until a byte landed
#endif

void pio_serve_interrupt(void) {
#ifdef POLY_HANDSHAKE_DIAG
    g_pio_irq_entries++;   // did the PIO1 IRQ fire on this (master) core AT ALL?
#endif
    uint32_t irqs = pio->ints0;
#ifdef POLY_HANDSHAKE_DIAG
    if (irqs & (PIO_IRQ0_INTF_SM0_RXNEMPTY_BITS << rx_state_machine)) g_pio_irq_rxne++;
#endif

    // The RX FIFO is not empty any more, therefore wake any sleeping rx thread
    if (irqs & (PIO_IRQ0_INTF_SM0_RXNEMPTY_BITS << rx_state_machine)) {
        // Disable rx not empty interrupt
        pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_state_machine, false);

        osalSysLockFromISR();
        osalThreadResumeI(&rx_thread, MSG_OK);
        osalSysUnlockFromISR();
    }

    // The TX FIFO is not full any more, therefore wake any sleeping tx thread
    if (irqs & (PIO_IRQ0_INTF_SM0_TXNFULL_BITS << tx_state_machine)) {
        // Disable tx not full interrupt
        pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full + tx_state_machine, false);
        osalSysLockFromISR();
        osalThreadResumeI(&tx_thread, MSG_OK);
        osalSysUnlockFromISR();
    }

    // IRQ 0 is set on framing or break errors by the rx state machine
    if (pio_interrupt_get(pio, 0UL)) {
        pio_interrupt_clear(pio, 0UL);
#ifdef POLY_HANDSHAKE_DIAG
        extern volatile uint32_t g_rx_framing_errors;
        g_rx_framing_errors++;   // count RX-SM framing/break errors (bad stop bit)
#endif
        osalSysLockFromISR();
        osalThreadResumeI(&rx_thread, MSG_PIO_ERROR);
        osalSysUnlockFromISR();
    }
}

#if !defined(SERIAL_USART_FULL_DUPLEX)
// The internal pull-ups of the RP2040 are rather weakish with a range of 50k to
// 80k, which in turn do not provide enough current to guarantee fast signal rise
// times with a parasitic capacitance of greater than 100pf. In real world
// applications, like split keyboards which might have vias in the signal path
// or long PCB traces, this prevents a successful communication. The solution
// is to temporarily augment the weak pull ups from the receiving side by
// driving the tx pin high. On the receiving side the lowest possible drive
// strength is chosen because the transmitting side must still be able to drive
// the signal low. With this configuration the rise times are fast enough and
// the generated low level with 360mV will generate a logical zero.
static void __no_inline_not_in_flash_func(enter_rx_state)(void) {
    osalSysLock();
    // Wait for the transmitting state machines FIFO to run empty. At this point
    // the last byte has been pulled from the transmitting state machines FIFO
    // into the output shift register. We have to wait a tiny bit more until
    // this byte is transmitted, before we can turn on the receiving state
    // machine again.
    while (!pio_sm_is_tx_fifo_empty(pio, tx_state_machine)) {
    }
    // Wait for ~11 bits, 1 start bit + 8 data bits + 1 stop bit + 1 bit
    // headroom.
    wait_us(1000000U * 11U / SERIAL_USART_SPEED);
    // Disable tx state machine to not interfere with our tx pin manipulation
    pio_sm_set_enabled(pio, tx_state_machine, false);
    gpio_set_drive_strength(SERIAL_USART_TX_PIN, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_pins_with_mask(pio, tx_state_machine, 1U << SERIAL_USART_TX_PIN, 1U << SERIAL_USART_TX_PIN);
    pio_sm_set_consecutive_pindirs(pio, tx_state_machine, SERIAL_USART_TX_PIN, 1U, false);
    pio_sm_set_enabled(pio, rx_state_machine, true);
    osalSysUnlock();
}

static void __no_inline_not_in_flash_func(leave_rx_state)(void) {
    osalSysLock();
    // In Half-duplex operation the tx pin dual-functions as sender and
    // receiver. To not receive the data we will send, we disable the receiving
    // state machine.
    pio_sm_set_enabled(pio, rx_state_machine, false);
    pio_sm_set_consecutive_pindirs(pio, tx_state_machine, SERIAL_USART_TX_PIN, 1U, true);
    pio_sm_set_pins_with_mask(pio, tx_state_machine, 0U, 1U << SERIAL_USART_TX_PIN);
    gpio_set_drive_strength(SERIAL_USART_TX_PIN, GPIO_DRIVE_STRENGTH_12MA);
    pio_sm_restart(pio, tx_state_machine);
    pio_sm_set_enabled(pio, tx_state_machine, true);
    osalSysUnlock();
}
#else
// All this trickery is gladly not necessary for full-duplex.
static inline void enter_rx_state(void) {}
static inline void leave_rx_state(void) {}
#endif

#ifdef POLY_HANDSHAKE_DIAG
// Diagnostic (split42 root-cause): report the master's RX side at the moment its
// echo-receive fails, to split "slave never drove GP5" from "echo reached the master
// RX FIFO but was never consumed (IRQ/wake failed)". Returns the RX FIFO level in the
// low bits; bit31 set = RX state machine was never even claimed (rx_state_machine<0).
uint32_t serial_debug_rx_fifo_level(void) {
    if (rx_state_machine < 0) {
        return 0x80000000u;
    }
    return pio_sm_get_rx_fifo_level(pio, rx_state_machine);
}
// Peek the oldest byte in the RX FIFO without removing it (0xFFFFFFFF if empty).
uint32_t serial_debug_rx_fifo_peek(void) {
    if (rx_state_machine < 0 || pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
        return 0xFFFFFFFFu;
    }
    // Same alignment the driver uses: the byte sits in the top octet of the word.
    return (uint32_t)(*((uint8_t*)&pio->rxf[rx_state_machine] + 3U));
}

// Is the slave PHYSICALLY driving the RX line? On the master, GP5 (the RX pin) idles
// HIGH — held up by this pin's own pull-up (PAL_RP_PAD_PUE). The only thing that pulls
// it LOW is the slave's TX actively sending (UART start bits). gpio_get() reads the raw
// pad input regardless of the PIO function mux, so a tight sampling burst right after
// the master sends the id (when the slave should be echoing) tells us TX-pin-alive vs
// dead — independent of whether the RX SM captures the byte.
static pin_t    s_dbg_rx_pin = 0;   // set in pio_rx_init
static uint     s_dbg_rx_offset = 0;   // RX program offset, for a diagnostic RX SM re-init
static bool     s_rx_ever_low = false;
static uint32_t s_rx_max_low_run = 0;   // longest consecutive-low sample run seen
static uint32_t s_rx_pc_moved = 0;      // # samples the RX PC was OFF the wait (offset 19)
static uint32_t s_rx_pc_max = 0;        // highest RX PC seen off the wait
static uint32_t s_rx_pc_min = 0xFFFFFFFF;// lowest RX PC seen off the wait
static uint32_t s_rx_min_low_us = 0xFFFFFFFF;  // shortest LOW pulse on GP5 (us) — true bit time
static uint32_t s_rx_min_high_us = 0xFFFFFFFF; // shortest HIGH pulse on GP5 (us)
static uint32_t s_rx_fifo_seen = 0;     // # burst samples the RX FIFO was non-empty (SM pushed)
void serial_debug_rx_sample_burst(void) {
    // Sample the raw RX pad in a tight loop (~40-60 ns/sample). Track the LONGEST run
    // of consecutive lows across all bursts: a valid UART start bit at this baud is
    // ~4.3 us -> ~80-100 consecutive lows; electrical glitches / crosstalk are 1-3
    // samples. So max_low_run tells "the slave sends a real frame" from "GP5 only
    // glitches" — which decides slave-TX-broken vs a master-RX silicon oddity.
    uint32_t run = 0;
    bool     last  = gpio_get(s_dbg_rx_pin);
    uint32_t lastt = timer_hw->timerawl;
    for (int i = 0; i < 16000; ++i) {
        bool lvl = gpio_get(s_dbg_rx_pin);
        // Reliable bit time: timestamp each GP5 edge and keep the SHORTEST low/high pulse
        // seen (in us). IRQs only ever stretch a pulse, so the min across many pulses is
        // the true bit period. Expected ~4 us at 230400 baud; ~2 us would be 2x baud.
        if (lvl != last) {
            uint32_t now = timer_hw->timerawl, w = now - lastt;
            if (w >= 1) {
                if (!last) { if (w < s_rx_min_low_us)  s_rx_min_low_us  = w; }
                else       { if (w < s_rx_min_high_us) s_rx_min_high_us = w; }
            }
            last = lvl; lastt = now;
        }
        if (!lvl) {
            s_rx_ever_low = true;
            if (++run > s_rx_max_low_run) s_rx_max_low_run = run;
        } else {
            run = 0;
        }
        // Did the RX SM actually PUSH a byte? Watch the RX FIFO level during the window
        // (nothing drains it here — the receive runs later). >0 ever = the SM pushed.
        if (rx_state_machine >= 0 && !pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
            s_rx_fifo_seen++;
        }
        // Also watch the RX SM's PC. If it ever leaves the start-bit wait (offset 19),
        // it DID detect a start and ran into the receive body (20..27) -> it sees the
        // signal but fails to FRAME the byte (baud / slave-TX timing mismatch). If it
        // never moves off 19, its input genuinely reads high -> a real input-path issue.
        if (rx_state_machine >= 0) {
            uint32_t pc = pio->sm[rx_state_machine].addr;
            if (pc != s_dbg_rx_offset) {
                s_rx_pc_moved++;
                if (pc > s_rx_pc_max) s_rx_pc_max = pc;
                if (pc < s_rx_pc_min) s_rx_pc_min = pc;
            }
        }
    }
}
bool     serial_debug_rx_ever_low(void)   { return s_rx_ever_low; }
uint32_t serial_debug_rx_max_low_run(void){ return s_rx_max_low_run; }
uint32_t serial_debug_rx_pc_moved(void)   { return s_rx_pc_moved; }
uint32_t serial_debug_rx_pc_span(void)    { return (s_rx_pc_moved ? ((s_rx_pc_min << 8) | (s_rx_pc_max & 0xFF)) : 0); }
uint32_t serial_debug_rx_min_low_us(void) { return (s_rx_min_low_us  == 0xFFFFFFFF) ? 0 : s_rx_min_low_us; }
uint32_t serial_debug_rx_min_high_us(void){ return (s_rx_min_high_us == 0xFFFFFFFF) ? 0 : s_rx_min_high_us; }
uint32_t serial_debug_rx_fifo_seen(void)  { return s_rx_fifo_seen; }
uint32_t serial_debug_rx_framing_errors(void){ return g_rx_framing_errors; }

// Called from initiate_transaction right BEFORE the normal serial_transport_receive.
// If a byte is already sitting in the RX FIFO at that instant, pop it directly (bypassing
// sync_rx) and remember it + count it. This decides the standing paradox: a valid byte
// present here means the SM+FIFO work and the sync_rx / IRQ-wake receive path is what
// fails to consume it; nothing present means the byte is gone before the receive runs.
static uint32_t s_rx_direct_last = 0xFFFF;  // last byte found waiting just before receive
static uint32_t s_rx_direct_hits = 0;       // # transactions a byte was waiting before receive
void serial_debug_rx_pop_before_recv(void) {
    if (rx_state_machine >= 0 && !pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
        s_rx_direct_last = (uint32_t)(*((uint8_t*)&pio->rxf[rx_state_machine] + 3U));
        s_rx_direct_hits++;
    }
}
uint32_t serial_debug_rx_direct_last(void){ return s_rx_direct_last; }
uint32_t serial_debug_rx_direct_hits(void){ return s_rx_direct_hits; }

// Read the master-core PIO1 IRQ counters (see pio_serve_interrupt): how often the handler
// ran at all, and how often it took the rx-not-empty branch. On the broken split42 link
// these stay ~0 even while bytes reach the RX FIFO -> the IRQ wake is what's dead.
uint32_t serial_debug_irq_entries(void){ return g_pio_irq_entries; }
uint32_t serial_debug_irq_rxne(void){ return g_pio_irq_rxne; }
// sync_rx pre-poll outcome (POLY_RX_POLL_FIX): how often the byte was caught by polling, how
// often the window expired empty, and the worst latency until a byte landed. hits>0 = the byte
// DOES arrive (just later than the old 1.5 ms window); max_us shows how much window is needed.
uint32_t serial_debug_poll_hits(void){ return g_poll_hits; }
uint32_t serial_debug_poll_miss(void){ return g_poll_miss; }
uint32_t serial_debug_poll_max_us(void){ return g_poll_max_us; }

// FIX EXPERIMENT: fully re-initialise the master's RX state machine. Theory (from the
// register dump + RP2040 forums): the RX SM comes up metastably-wedged at init on some
// boots (non-deterministic) — it READS as parked at `wait 0 pin` but its execution FFs
// are malfunctioning, so it never responds to the valid start bits physically present on
// GP5. A metastable SM only recovers via a real re-init. This disables it, clears the
// FIFO, restarts the SM internal state + clock divider (fresh phase), forces the PC back
// to the program start, and re-enables. If the link comes up after this, the root cause
// is a bad SM power-on/init state that pointing's different init timing happens to dodge.
void serial_debug_reinit_rx(void) {
    if (rx_state_machine < 0) return;
    pio_sm_set_enabled(pio, rx_state_machine, false);
    pio_sm_clear_fifos(pio, rx_state_machine);
    pio_sm_restart(pio, rx_state_machine);
    pio_sm_clkdiv_restart(pio, rx_state_machine);
    pio_sm_exec(pio, rx_state_machine, pio_encode_jmp(s_dbg_rx_offset));
    pio_sm_set_enabled(pio, rx_state_machine, true);
}

// Dump the live PIO1 registers for BOTH serial state machines on the master. The TX SM
// (GP4) works and the RX SM (GP5) does not, on the same PIO block — so comparing them
// pinpoints what's wrong with the RX SM: enabled? on the right pin? PC advancing (i.e.
// is the program actually running, or stalled)? Reads pc twice so a running SM shows two
// different values. Decode after: ctrl bits0-3 = per-SM enable; pinctrl IN_BASE = bits0-4;
// execctrl JMP_PIN = bits24-28; fstat RXEMPTY = bits8-11 (per SM).
void serial_debug_dump_rx_sm(void) {
    uprintf("PIO1 ctrl=0x%08lX fstat=0x%08lX flevel=0x%08lX fdebug=0x%08lX tx_sm=%d rx_sm=%d\n",
            (unsigned long)pio->ctrl, (unsigned long)pio->fstat, (unsigned long)pio->flevel,
            (unsigned long)pio->fdebug, tx_state_machine, rx_state_machine);
    if (tx_state_machine >= 0) {
        int s = tx_state_machine;
        uprintf("  TX sm=%d pinctrl=0x%08lX execctrl=0x%08lX clkdiv=0x%08lX pc=%lu\n",
                s, (unsigned long)pio->sm[s].pinctrl, (unsigned long)pio->sm[s].execctrl,
                (unsigned long)pio->sm[s].clkdiv, (unsigned long)pio->sm[s].addr);
    }
    if (rx_state_machine >= 0) {
        int s = rx_state_machine;
        uint32_t pc0 = pio->sm[s].addr, pc1 = pio->sm[s].addr;
        uprintf("  RX sm=%d pinctrl=0x%08lX execctrl=0x%08lX clkdiv=0x%08lX pc=%lu,%lu\n",
                s, (unsigned long)pio->sm[s].pinctrl, (unsigned long)pio->sm[s].execctrl,
                (unsigned long)pio->sm[s].clkdiv, (unsigned long)pc0, (unsigned long)pc1);
    }
    // GP4 (master TX, working) vs GP5 (master RX, dead) IO-mux + pad registers. The RX
    // SM is correctly configured yet never sees GP5 go low — so the PIO's input view of
    // GP5 is stuck. Compare against GP4: io ctrl FUNCSEL=[4:0] (PIO1=7), INOVER=[17:16]
    // (0=normal; 2/3 force the *peripheral* input low/high -> would freeze the RX wait);
    // pad IE=bit6 (input enable), OD=bit7 (output disable). gpio_get_function() decodes
    // FUNCSEL. A GP5 INOVER!=0 or IE=0 (while GP4 is clean) is the clobber we're hunting.
    uprintf("  GP4 func=%d ioctrl=0x%08lX pad=0x%08lX | GP5 func=%d ioctrl=0x%08lX pad=0x%08lX\n",
            (int)gpio_get_function(4), (unsigned long)iobank0_hw->io[4].ctrl, (unsigned long)padsbank0_hw->io[4],
            (int)gpio_get_function(5), (unsigned long)iobank0_hw->io[5].ctrl, (unsigned long)padsbank0_hw->io[5]);
    // The decisive one: is PIO1 DRIVING its own RX pin? dbg_padoe = per-GPIO output-enable
    // the PIO asserts, dbg_padout = the value it drives. If padoe bit5 (GP5) is 1, the
    // master is driving its RX line (should be input) -> it fights the slave's start-bit
    // lows, so the RX `wait 0 pin` mostly sees high and never triggers. bit4 (GP4/TX)
    // is expected 1. input_sync_bypass should be 0 (synchroniser active).
    uprintf("  PIO1 padoe=0x%08lX padout=0x%08lX in_sync_bypass=0x%08lX clk_sys=%luHz  (GP4=bit4 GP5=bit5)\n",
            (unsigned long)pio->dbg_padoe, (unsigned long)pio->dbg_padout,
            (unsigned long)pio->input_sync_bypass, (unsigned long)clock_get_hz(clk_sys));
    // The IRQ path: inte0 = which sources are enabled to raise PIO1 IRQ0; ints0 = which are
    // asserting right now. NVIC ISER[0]/ISPR[0] = is the PIO1 IRQ vector enabled / pending on
    // THIS (master) core. If ints0 shows rx-not-empty asserting but the handler never runs
    // (irq_entries=0), the NVIC line is masked/disabled on core0 -> that's the broken wake.
    uprintf("  PIO1 inte0=0x%08lX ints0=0x%08lX  NVIC ISER0=0x%08lX ISPR0=0x%08lX  irq_entries=%lu irq_rxne=%lu\n",
            (unsigned long)pio->inte0, (unsigned long)pio->ints0,
            (unsigned long)NVIC->ISER[0], (unsigned long)NVIC->ISPR[0],
            (unsigned long)g_pio_irq_entries, (unsigned long)g_pio_irq_rxne);
}
#endif

/**
 * @brief Clear the FIFO of the RX state machine.
 */
inline void serial_transport_driver_clear(void) {
    osalSysLock();
    while (!pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
        pio_sm_clear_fifos(pio, rx_state_machine);
    }
    osalSysUnlock();
}

static inline msg_t sync_tx(sysinterval_t timeout) {
    msg_t msg = MSG_OK;
    osalSysLock();
    while (pio_sm_is_tx_fifo_full(pio, tx_state_machine)) {
        pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full + tx_state_machine, true);
        msg = osalThreadSuspendTimeoutS(&tx_thread, timeout);
        if (msg < MSG_OK) {
            pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full + tx_state_machine, false);
            break;
        }
    }
    osalSysUnlock();
    return msg;
}

static inline bool send_impl(const uint8_t* source, const size_t size) {
    size_t send = 0;
    msg_t  msg;
#ifdef POLY_SLAVE_STAGE_PROBE
    poly_slave_stage_probe(3);   // entered send_impl
#endif
    while (send < size) {
        msg = sync_tx(TIME_MS2I(SERIAL_USART_TIMEOUT));
#ifdef POLY_SLAVE_STAGE_PROBE
        poly_slave_stage_probe(4);   // sync_tx returned (did NOT block forever)
#endif
        if (msg < MSG_OK) {
#ifdef POLY_SLAVE_STAGE_PROBE
            poly_slave_stage_probe(7);   // sync_tx TIMED OUT -> TX FIFO stayed full
#endif
            return false;
        }

        osalSysLock();
        while (send < size) {
            if (pio_sm_is_tx_fifo_full(pio, tx_state_machine)) {
                break;
            }
            if (send >= size) {
                break;
            }
            pio_sm_put(pio, tx_state_machine, (uint32_t)(*source));
            source++;
            send++;
        }
        osalSysUnlock();
#ifdef POLY_SLAVE_STAGE_PROBE
        poly_slave_stage_probe(5);   // byte(s) written to the TX FIFO (pio_sm_put ran)
#endif
    }

    return send == size;
}

/**
 * @brief Blocking send of buffer with timeout.
 *
 * @return true Send success.
 * @return false Send failed.
 */
inline bool serial_transport_send(const uint8_t* source, const size_t size) {
#ifdef POLY_SLAVE_STAGE_PROBE
    poly_slave_stage_probe(2);   // entered serial_transport_send
#endif
    leave_rx_state();
    bool result = send_impl(source, size);
    enter_rx_state();

    return result;
}

static inline msg_t sync_rx(sysinterval_t timeout) {
    msg_t msg = MSG_OK;
#ifdef POLY_RX_POLL_FIX
    // FIX (split42 root cause): the master's PIO1 rx-not-empty IRQ effectively never fires
    // (irq_entries ~0 across hundreds of transactions), even though NVIC PIO1_IRQ_0 is
    // enabled and the RX SM decodes + pushes the echo byte into the FIFO every time. So the
    // IRQ-driven suspend below waits the full 20 ms for a wake that never comes, and the
    // receive only succeeds when a byte happens to be waiting already. Enabling the pointing
    // device merely shifted timing so a byte was pre-loaded — masking, not fixing, this.
    //
    // Don't depend on the IRQ: poll the RX FIFO (unlocked, IRQs on) up to POLY_RX_POLL_US.
    // Because only this thread drains the master RX FIFO, seeing non-empty here means the very
    // next locked empty-check consumes the byte with no suspend. A genuinely dead link still
    // falls through to the normal IRQ-suspend timeout below, so this can only help. Also
    // measure whether/when the byte lands (poll hit/miss + worst latency), so we can tell
    // "byte arrives, just later than the old window" from "byte never arrives at all".
    if (rx_state_machine >= 0 && pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
        uint32_t t0  = timer_hw->timerawl;
        uint32_t dt  = 0;
        bool     got = false;
        while (dt < (uint32_t)POLY_RX_POLL_US) {
            if (!pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) { got = true; break; }
            dt = timer_hw->timerawl - t0;
        }
#    ifdef POLY_HANDSHAKE_DIAG
        if (got) {
            g_poll_hits++;
            if (dt > g_poll_max_us) g_poll_max_us = dt;
        } else {
            g_poll_miss++;
        }
#    endif
    }
#endif
    osalSysLock();
    while (pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
        pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_state_machine, true);
        msg = osalThreadSuspendTimeoutS(&rx_thread, timeout);
        if (msg < MSG_OK) {
            pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_state_machine, false);
            break;
        }
    }
    osalSysUnlock();
    return msg;
}

static inline bool receive_impl(uint8_t* destination, const size_t size, sysinterval_t timeout) {
    size_t read = 0U;

    while (read < size) {
        msg_t msg = sync_rx(timeout);
        if (msg < MSG_OK) {
            return false;
        }
        osalSysLock();
        while (true) {
            if (pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
                break;
            }
            if (read >= size) {
                break;
            }
            *destination++ = *((uint8_t*)&pio->rxf[rx_state_machine] + 3U);
            read++;
        }
        osalSysUnlock();
    }

    return read == size;
}

/**
 * @brief  Blocking receive of size * bytes with timeout.
 *
 * @return true Receive success.
 * @return false Receive failed, e.g. by timeout.
 */
inline bool serial_transport_receive(uint8_t* destination, const size_t size) {
    return receive_impl(destination, size, TIME_MS2I(SERIAL_USART_TIMEOUT));
}

/**
 * @brief  Blocking receive of size * bytes.
 *
 * @return true Receive success.
 * @return false Receive failed.
 */
inline bool serial_transport_receive_blocking(uint8_t* destination, const size_t size) {
    return receive_impl(destination, size, TIME_INFINITE);
}

static inline void pio_tx_init(pin_t tx_pin) {
    uint pio_idx = pio_get_index(pio);
    uint offset  = pio_add_program(pio, &uart_tx_program);

#if defined(SERIAL_USART_FULL_DUPLEX)
    // clang-format off
    iomode_t tx_pin_mode = PAL_RP_GPIO_OE |
                           PAL_RP_PAD_SLEWFAST |
                           PAL_RP_PAD_DRIVE4 |
                           (pio_idx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    pio_sm_set_pins_with_mask(pio, tx_state_machine, 1U << tx_pin, 1U << tx_pin);
    pio_sm_set_consecutive_pindirs(pio, tx_state_machine, tx_pin, 1U, true);
#else
    // clang-format off
    iomode_t tx_pin_mode = PAL_RP_PAD_IE |
                           PAL_RP_GPIO_OE |
                           PAL_RP_PAD_SCHMITT |
                           PAL_RP_PAD_PUE |
                           PAL_RP_PAD_SLEWFAST |
                           PAL_RP_PAD_DRIVE12 |
                           PAL_RP_IOCTRL_OEOVER_DRVINVPERI |
                           (pio_idx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    pio_sm_set_pins_with_mask(pio, tx_state_machine, 0U << tx_pin, 1U << tx_pin);
    pio_sm_set_consecutive_pindirs(pio, tx_state_machine, tx_pin, 1U, true);
#endif

    palSetLineMode(tx_pin, tx_pin_mode);

    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offset + UART_TX_WRAP_TARGET, offset + UART_TX_WRAP);
#if defined(SERIAL_USART_FULL_DUPLEX)
    sm_config_set_sideset(&config, 2, true, false);
#else
    sm_config_set_sideset(&config, 2, true, true);
#endif
    // OUT shifts to right, no autopull
    sm_config_set_out_shift(&config, true, false, 32);
    // We are mapping both OUT and side-set to the same pin, because sometimes
    // we need to assert user data onto the pin (with OUT) and sometimes
    // assert constant values (start/stop bit)
    sm_config_set_out_pins(&config, tx_pin, 1);
    sm_config_set_sideset_pins(&config, tx_pin);
    // We only need TX, so get an 8-deep FIFO!
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    // SM transmits 1 bit per 8 execution cycles.
#ifdef POLY_FIXED_SERIAL_CLKDIV
    // FIX TEST (split42 root cause): compute the PIO clock divisor from a FIXED clock
    // constant instead of the live clock_get_hz(clk_sys). Symptom traced: the master RX
    // detects every start bit but never frames a byte, and the measured slave bit width
    // varies boot-to-boot (gp5_low_run 36..102) -> the two halves' baud drift apart. The
    // suspected cause is clock_get_hz(clk_sys) returning a not-yet-final frequency at init
    // on one half some boots, so its baud differs from the other half. Pinning the divisor
    // to a constant makes BOTH halves transmit/receive at the exact same rate regardless of
    // the clock state at init, so they can't drift. 125 MHz matches what the master computes
    // in the field (clkdiv 0x0043D100). Guarded; split72/normal keep the live computation.
    float div = 125000000.0f / (8.0f * (float)SERIAL_USART_SPEED);
#else
    float div = (float)clock_get_hz(clk_sys) / (8 * SERIAL_USART_SPEED);
#endif
    sm_config_set_clkdiv(&config, div);
    pio_sm_init(pio, tx_state_machine, offset, &config);
    pio_sm_set_enabled(pio, tx_state_machine, true);
}

static inline void pio_rx_init(pin_t rx_pin) {
#ifdef POLY_HANDSHAKE_DIAG
    s_dbg_rx_pin = rx_pin;   // remember which pin to sample (master: GP5)
#endif
    uint offset = pio_add_program(pio, &uart_rx_program);
#ifdef POLY_HANDSHAKE_DIAG
    s_dbg_rx_offset = offset;
#endif

#if defined(SERIAL_USART_FULL_DUPLEX)
    uint pio_idx = pio_get_index(pio);
    pio_sm_set_consecutive_pindirs(pio, rx_state_machine, rx_pin, 1, false);
    // clang-format off
    iomode_t rx_pin_mode = PAL_RP_PAD_IE |
                           PAL_RP_PAD_SCHMITT |
                           PAL_RP_PAD_PUE |
                           (pio_idx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    palSetLineMode(rx_pin, rx_pin_mode);
#endif

    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offset + UART_RX_WRAP_TARGET, offset + UART_RX_WRAP);
    sm_config_set_in_pins(&config, rx_pin); // for WAIT, IN
    sm_config_set_jmp_pin(&config, rx_pin); // for JMP
    // Shift to right, autopush disabled
    sm_config_set_in_shift(&config, true, false, 32);
    // Deeper FIFO as we're not doing any TX
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    // SM transmits 1 bit per 8 execution cycles.
#ifdef POLY_FIXED_SERIAL_CLKDIV
    // FIX TEST (split42 root cause): compute the PIO clock divisor from a FIXED clock
    // constant instead of the live clock_get_hz(clk_sys). Symptom traced: the master RX
    // detects every start bit but never frames a byte, and the measured slave bit width
    // varies boot-to-boot (gp5_low_run 36..102) -> the two halves' baud drift apart. The
    // suspected cause is clock_get_hz(clk_sys) returning a not-yet-final frequency at init
    // on one half some boots, so its baud differs from the other half. Pinning the divisor
    // to a constant makes BOTH halves transmit/receive at the exact same rate regardless of
    // the clock state at init, so they can't drift. 125 MHz matches what the master computes
    // in the field (clkdiv 0x0043D100). Guarded; split72/normal keep the live computation.
    float div = 125000000.0f / (8.0f * (float)SERIAL_USART_SPEED);
#else
    float div = (float)clock_get_hz(clk_sys) / (8 * SERIAL_USART_SPEED);
#endif
    sm_config_set_clkdiv(&config, div);
    pio_sm_init(pio, rx_state_machine, offset, &config);
    pio_sm_set_enabled(pio, rx_state_machine, true);
}

static inline void pio_init(pin_t tx_pin, pin_t rx_pin) {
    uint pio_idx = pio_get_index(pio);

    /* Get PIOx peripheral out of reset state. */
    hal_lld_peripheral_unreset(pio_idx == 0 ? RESETS_ALLREG_PIO0 : RESETS_ALLREG_PIO1);

    tx_state_machine = pio_claim_unused_sm(pio, true);
    if (tx_state_machine < 0) {
        dprintln("ERROR: Failed to acquire state machine for serial transmission!");
        return;
    }
    pio_tx_init(tx_pin);

    rx_state_machine = pio_claim_unused_sm(pio, true);
    if (rx_state_machine < 0) {
        dprintln("ERROR: Failed to acquire state machine for serial reception!");
        return;
    }
    pio_rx_init(rx_pin);

    // Enable error flag IRQ source for rx state machine
    pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_state_machine, true);
    pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full + tx_state_machine, true);
    pio_set_irq0_source_enabled(pio, pis_interrupt0, true);

    // Enable PIO specific interrupt vector, as the pio implementation is timing
    // critical we use the highest possible priority.
#if defined(SERIAL_PIO_USE_PIO1)
    nvicEnableVector(RP_PIO1_IRQ_0_NUMBER, CORTEX_MAX_KERNEL_PRIORITY);
#else
    nvicEnableVector(RP_PIO0_IRQ_0_NUMBER, CORTEX_MAX_KERNEL_PRIORITY);
#endif

    enter_rx_state();
}

/**
 * @brief PIO driver specific initialization function for the master side.
 */
void serial_transport_driver_master_init(void) {
#if defined(SERIAL_USART_FULL_DUPLEX)
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_RX_PIN;
#else
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_TX_PIN;
#endif

#if defined(SERIAL_USART_PIN_SWAP)
    pio_init(rx_pin, tx_pin);
#else
    pio_init(tx_pin, rx_pin);
#endif
}

/**
 * @brief PIO driver specific initialization function for the slave side.
 */
void serial_transport_driver_slave_init(void) {
#if defined(SERIAL_USART_FULL_DUPLEX)
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_RX_PIN;
#else
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_TX_PIN;
#endif

    pio_init(tx_pin, rx_pin);
}
