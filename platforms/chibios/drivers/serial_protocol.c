// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ch.h>

#include "serial.h"
#include "serial_protocol.h"
#include "synchronization_util.h"
#ifdef POLY_HANDSHAKE_DIAG
#    include "print.h"
// Implemented in serial_vendor.c: master RX-side state at the moment the echo receive
// fails, to split "slave never drove GP5" (FIFO empty) from "echo reached the master RX
// FIFO but was never consumed / arrived after the 20 ms window" (byte present).
extern uint32_t serial_debug_rx_fifo_level(void);
extern uint32_t serial_debug_rx_fifo_peek(void);
// Sample the RX line right after the master sends its id (slave echo window) + report
// whether GP5 has EVER been pulled low by the slave (across all transactions so far).
extern void     serial_debug_rx_sample_burst(void);
extern bool     serial_debug_rx_ever_low(void);
extern uint32_t serial_debug_rx_max_low_run(void);
extern uint32_t serial_debug_rx_pc_moved(void); // # samples the RX PC left the wait (offset 19)
extern uint32_t serial_debug_rx_pc_span(void);  // (min<<8)|max PC seen off the wait, or 0
extern uint32_t serial_debug_rx_min_low_us(void);  // shortest GP5 low pulse (us) = true bit time
extern uint32_t serial_debug_rx_min_high_us(void); // shortest GP5 high pulse (us)
extern uint32_t serial_debug_rx_fifo_seen(void);   // # window samples the RX FIFO was non-empty
extern uint32_t serial_debug_rx_framing_errors(void); // RX-SM framing/break errors (bad stop bit)
extern void     serial_debug_rx_pop_before_recv(void); // pop the RX FIFO right before the receive
extern uint32_t serial_debug_rx_direct_last(void);     // last byte found waiting before receive
extern uint32_t serial_debug_rx_direct_hits(void);     // # txns a byte was waiting before receive
extern void     serial_debug_dump_rx_sm(void);
extern void     serial_debug_reinit_rx(void);   // FIX EXPERIMENT: re-init a wedged RX SM
extern uint32_t serial_debug_irq_entries(void); // # times the master PIO1 IRQ handler ran
extern uint32_t serial_debug_irq_rxne(void);    // # times it took the rx-not-empty branch
extern uint32_t serial_debug_poll_hits(void);   // # sync_rx polls that caught a byte in-window
extern uint32_t serial_debug_poll_miss(void);   // # sync_rx polls whose window expired empty
extern uint32_t serial_debug_poll_max_us(void); // worst poll latency (us) until a byte landed
#    ifndef POLY_RX_POLL_US
#        define POLY_RX_POLL_US 0
#    endif
#endif
#ifdef POLY_SLAVE_STAGE_PROBE
// Implemented in keyboards/polykybd/poly_util.c (guarded by the same define). Draws
// the furthest react_to_transaction stage reached to this half's keycaps so a frozen
// slave localises its own wedge. See that function for the stage legend.
extern void poly_slave_stage_probe(uint8_t stage);
#endif

static inline bool initiate_transaction(uint8_t transaction_id);
static inline bool react_to_transaction(void);

/**
 * @brief This thread runs on the slave and responds to transactions initiated
 * by the master.
 */
static THD_WORKING_AREA(waSlaveThread, 1024);
static THD_FUNCTION(SlaveThread, arg) {
    (void)arg;
    chRegSetThreadName("split_protocol_tx_rx");

    while (true) {
        if (unlikely(!react_to_transaction())) {
            /* Clear the receive queue, to start with a clean slate.
             * Parts of failed transactions or spurious bytes could still be in it. */
            serial_transport_driver_clear();
        }
    }
}

/**
 * @brief Slave specific initializations.
 */
void soft_serial_target_init(void) {
    serial_transport_driver_slave_init();

    /* Start transport thread. */
    chThdCreateStatic(waSlaveThread, sizeof(waSlaveThread), HIGHPRIO, SlaveThread, NULL);
}

/**
 * @brief Master specific initializations.
 */
void soft_serial_initiator_init(void) {
    serial_transport_driver_master_init();
}

/**
 * @brief React to transactions started by the master.
 */
static inline bool react_to_transaction(void) {
    uint8_t transaction_id = 0;
    /* Wait until there is a transaction for us. */
    if (unlikely(!serial_transport_receive_blocking(&transaction_id, sizeof(transaction_id)))) {
        return false;
    }

    /* Sanity check that we are actually responding to a valid transaction. */
    if (unlikely(transaction_id >= NUM_TOTAL_TRANSACTIONS)) {
        return false;
    }

    split_shared_memory_lock_autounlock();

    split_transaction_desc_t* transaction = &split_transaction_table[transaction_id];

    /* Send back the handshake which is XORed as a simple checksum,
     to signal that the slave is ready to receive possible transaction buffers  */
    transaction_id ^= NUM_TOTAL_TRANSACTIONS;
#ifdef POLY_SLAVE_STAGE_PROBE
    // Stages 1..3 (RX / lock / glyph) are CONFIRMED working — dropped. The probe now
    // traces INSIDE the echo transmit (the confirmed failure point). Marker 1 = "about
    // to call serial_transport_send"; markers 2..6 fire inside serial_vendor.c; markers
    // 6/7/8 report the echo outcome. See poly_util.c for the keycap map.
    poly_slave_stage_probe(1);   // about to echo (== old stage 4)
#endif
    if (unlikely(!serial_transport_send(&transaction_id, sizeof(transaction_id)))) {
#ifdef POLY_SLAVE_STAGE_PROBE
        poly_slave_stage_probe(8);   // serial_transport_send returned FALSE overall
#endif
        return false;
    }
#ifdef POLY_SLAVE_STAGE_PROBE
    poly_slave_stage_probe(6);   // serial_transport_send returned TRUE (echo "done")
#endif

    /* Receive transaction buffer from the master. If this transaction requires it.*/
    if (transaction->initiator2target_buffer_size) {
        if (unlikely(!serial_transport_receive(split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size))) {
            return false;
        }
    }

    /* Allow any slave processing to occur. */
    if (transaction->slave_callback) {
        transaction->slave_callback(transaction->initiator2target_buffer_size, split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size, split_trans_target2initiator_buffer(transaction));
    }

    /* Send transaction buffer to the master. If this transaction requires it. */
    if (transaction->target2initiator_buffer_size) {
        if (unlikely(!serial_transport_send(split_trans_target2initiator_buffer(transaction), transaction->target2initiator_buffer_size))) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Start transaction from the master half to the slave half.
 *
 * @param index Transaction Table index of the transaction to start.
 * @return bool Indicates success of transaction.
 */
bool soft_serial_transaction(int index) {
    /* Clear the receive queue, to start with a clean slate.
     * Parts of failed transactions or spurious bytes could still be in it. */
    serial_transport_driver_clear();

    return initiate_transaction((uint8_t)index);
}

/**
 * @brief Initiate transaction to slave half.
 */
static inline bool initiate_transaction(uint8_t transaction_id) {
    /* Sanity check that we are actually starting a valid transaction. */
    if (unlikely(transaction_id >= NUM_TOTAL_TRANSACTIONS)) {
        serial_dprintf("SPLIT: illegal transaction id\n");
        return false;
    }

    split_shared_memory_lock_autounlock();

    split_transaction_desc_t* transaction = &split_transaction_table[transaction_id];

    /* Send transaction table index to the slave, which doubles as basic handshake token. */
    if (unlikely(!serial_transport_send(&transaction_id, sizeof(transaction_id)))) {
        serial_dprintf("SPLIT: sending handshake failed\n");
        return false;
    }

    // NB: serial_debug_rx_sample_burst() is deliberately NOT called here. It is a ~2 ms
    // master busy-spin per transaction that (a) makes the whole keyboard sluggish / stalls
    // slave->master traffic and (b) confounds the measurement by itself delaying the receive
    // long enough for the echo to land. We want the UNPERTURBED receive behaviour.
    (void)0;

    uint8_t transaction_id_shake = 0xFF;

    /* Which we always read back first so that we can error out correctly.
     *   - due to the half duplex limitations on return codes, we always have to read *something*.
     *   - without the read, write only transactions *always* succeed, even during the boot process where the slave is not ready.
     */
    bool hs_rx_ok = serial_transport_receive(&transaction_id_shake, sizeof(transaction_id_shake));
    if (unlikely(!hs_rx_ok || (transaction_id_shake != (transaction_id ^ NUM_TOTAL_TRANSACTIONS)))) {
        serial_dprintf("SPLIT: receiving handshake failed\n");
#ifdef POLY_HANDSHAKE_DIAG
        // Diagnostic (split42 root-cause): distinguish a SILENT slave (rx timed out, no
        // byte) from a slave that echoes GARBAGE (a byte arrived but mismatched). Master-
        // side, throttled uprintf so it surfaces without debug_enable.
        {
            static uint32_t hs_timeout = 0, hs_garbage = 0, hs_total = 0;
            if (!hs_rx_ok) { hs_timeout++; } else { hs_garbage++; }
            if ((++hs_total % 500) == 0) {
                // Sample the master RX FIFO right now (post-failed-receive): does the
                // slave's echo actually reach us on GP5 but go unconsumed, or never
                // arrive at all? bit31 in level = RX state machine was never claimed.
                uint32_t rxlvl  = serial_debug_rx_fifo_level();
                (void)serial_debug_rx_fifo_peek;
                uint32_t span = serial_debug_rx_pc_span();
                uprintf("HS-DIAG: total=%lu timeout=%lu exp=0x%02X rx_fifo=%lu pc_moved=%lu pc=%lu..%lu min_low_us=%lu min_high_us=%lu fifo_seen=%lu\n",
                        (unsigned long)hs_total, (unsigned long)hs_timeout,
                        (unsigned)(uint8_t)(transaction_id ^ NUM_TOTAL_TRANSACTIONS),
                        (unsigned long)rxlvl,
                        (unsigned long)serial_debug_rx_pc_moved(),
                        (unsigned long)(span >> 8), (unsigned long)(span & 0xFF),
                        (unsigned long)serial_debug_rx_min_low_us(),
                        (unsigned long)serial_debug_rx_min_high_us(),
                        (unsigned long)serial_debug_rx_fifo_seen());
                uprintf("         framing_errors=%lu poll_hits=%lu poll_miss=%lu poll_max_us=%lu irq_entries=%lu irq_rxne=%lu  (poll_hits>0 => the echo DOES reach the FIFO, poll_max_us = how late; poll_miss w/ hits=0 => never arrives in %u us)\n",
                        (unsigned long)serial_debug_rx_framing_errors(),
                        (unsigned long)serial_debug_poll_hits(),
                        (unsigned long)serial_debug_poll_miss(),
                        (unsigned long)serial_debug_poll_max_us(),
                        (unsigned long)serial_debug_irq_entries(),
                        (unsigned long)serial_debug_irq_rxne(),
                        (unsigned)POLY_RX_POLL_US);
                // One-shot PIO register dump: compare the working TX SM against the
                // dead RX SM on the same PIO block to see what's mis-set-up.
                static bool dumped = false;
                if (!dumped) { dumped = true; serial_debug_dump_rx_sm(); }
                // (RX re-init experiment removed — it did NOT recover, so a bad SM state
                //  is ruled out, consistent with the forum: synchronisers on, clean PC.)
                (void)serial_debug_reinit_rx;
            }
        }
#endif
        return false;
    }

#ifdef POLY_HANDSHAKE_DIAG
    // SUCCESS path (link is up). Periodically report the master PIO1 IRQ counters so we can
    // tell whether the receive path now works because the IRQ is actually firing, or only
    // because the byte is pre-loaded (poll-fix / pointing-timing) while the IRQ stays dead.
    // irq_entries staying ~0 here while the link works confirms the IRQ is genuinely broken
    // and merely bypassed — not repaired.
    {
        static uint32_t hs_ok = 0;
        if ((++hs_ok % 2000) == 0) {
            uprintf("HS-OK: ok=%lu irq_entries=%lu irq_rxne=%lu poll_hits=%lu poll_miss=%lu poll_max_us=%lu  (irq_entries~0 while link works => rx IRQ still dead, bypassed by the poll; poll_max_us = how late the echo lands)\n",
                    (unsigned long)hs_ok,
                    (unsigned long)serial_debug_irq_entries(),
                    (unsigned long)serial_debug_irq_rxne(),
                    (unsigned long)serial_debug_poll_hits(),
                    (unsigned long)serial_debug_poll_miss(),
                    (unsigned long)serial_debug_poll_max_us());
        }
    }
#endif

    /* Send transaction buffer to the slave. If this transaction requires it. */
    if (transaction->initiator2target_buffer_size) {
        if (unlikely(!serial_transport_send(split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size))) {
            serial_dprintf("SPLIT: sending buffer failed\n");
            return false;
        }
    }

    /* Receive transaction buffer from the slave. If this transaction requires it. */
    if (transaction->target2initiator_buffer_size) {
        if (unlikely(!serial_transport_receive(split_trans_target2initiator_buffer(transaction), transaction->target2initiator_buffer_size))) {
            serial_dprintf("SPLIT: receiving buffer failed\n");
            return false;
        }
    }

    return true;
}
