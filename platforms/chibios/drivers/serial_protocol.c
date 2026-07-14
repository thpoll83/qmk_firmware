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
extern void     serial_debug_dump_rx_sm(void);
extern void     serial_debug_reinit_rx(void);   // FIX EXPERIMENT: re-init a wedged RX SM
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

#ifdef POLY_HANDSHAKE_DIAG
    // We've just sent the id; the slave should be echoing on GP5 right about now.
    // Burst-sample the raw line so we can tell "slave physically drives GP5" (ever
    // low) from "slave TX pin dead" (line stays high on our pull-up alone).
    serial_debug_rx_sample_burst();
#endif

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
                uint32_t rxpeek = serial_debug_rx_fifo_peek();
                uprintf("HS-DIAG: total=%lu timeout(no-rx)=%lu garbage(wrong-byte)=%lu last=0x%02X exp=0x%02X rx_fifo=%lu peek=0x%02lX gp5_ever_low=%u gp5_max_low_run=%lu\n",
                        (unsigned long)hs_total, (unsigned long)hs_timeout, (unsigned long)hs_garbage,
                        (unsigned)transaction_id_shake, (unsigned)(uint8_t)(transaction_id ^ NUM_TOTAL_TRANSACTIONS),
                        (unsigned long)rxlvl, (unsigned long)rxpeek, (unsigned)serial_debug_rx_ever_low(),
                        (unsigned long)serial_debug_rx_max_low_run());
                // One-shot PIO register dump: compare the working TX SM against the
                // dead RX SM on the same PIO block to see what's mis-set-up.
                static bool dumped = false;
                if (!dumped) { dumped = true; serial_debug_dump_rx_sm(); }
                // FIX EXPERIMENT: after 500 confirmed failures, fully re-init the RX SM
                // ONCE. If the metastable-wedge theory is right, the link comes up right
                // after this and the timeout count stops climbing. Watch the next lines.
                static bool reinit_done = false;
                if (!reinit_done) {
                    reinit_done = true;
                    serial_debug_reinit_rx();
                    uprintf("RX-REINIT: master RX state machine re-initialised — watch if the link recovers\n");
                }
            }
        }
#endif
        return false;
    }

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
