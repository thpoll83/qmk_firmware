// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ch.h>

#include "serial.h"
#include "serial_protocol.h"
#include "synchronization_util.h"
#ifdef POLY_HANDSHAKE_DIAG
#    include "print.h"
// split42 split-link diagnostics (implemented in serial_vendor.c). Observation only.
extern uint32_t serial_debug_irq_entries(void);
extern uint32_t serial_debug_irq_rxne(void);
extern uint32_t serial_debug_poll_hits(void);
extern uint32_t serial_debug_poll_miss(void);
extern uint32_t serial_debug_poll_max_us(void);
extern uint32_t serial_debug_rx_fifo_level(void);
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
    if (unlikely(!serial_transport_send(&transaction_id, sizeof(transaction_id)))) {
        return false;
    }

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

    uint8_t transaction_id_shake = 0xFF;

    /* Which we always read back first so that we can error out correctly.
     *   - due to the half duplex limitations on return codes, we always have to read *something*.
     *   - without the read, write only transactions *always* succeed, even during the boot process where the slave is not ready.
     */
    bool hs_rx_ok = serial_transport_receive(&transaction_id_shake, sizeof(transaction_id_shake));
    if (unlikely(!hs_rx_ok || (transaction_id_shake != (transaction_id ^ NUM_TOTAL_TRANSACTIONS)))) {
        serial_dprintf("SPLIT: receiving handshake failed\n");
#ifdef POLY_HANDSHAKE_DIAG
        // split42 diagnostic: on a failed handshake, report whether the slave was SILENT
        // (rx timed out, no byte) or echoed GARBAGE (a byte arrived but mismatched), plus the
        // RX-FIFO / poll / IRQ state. Throttled uprintf so it surfaces without debug_enable.
        {
            static uint32_t hs_timeout = 0, hs_garbage = 0, hs_total = 0;
            if (!hs_rx_ok) { hs_timeout++; } else { hs_garbage++; }
            if ((++hs_total % 500) == 0) {
                uprintf("HS-DIAG: total=%lu timeout=%lu garbage=%lu exp=0x%02X rx_fifo=%lu poll_hits=%lu poll_miss=%lu poll_max_us=%lu irq_entries=%lu irq_rxne=%lu\n",
                        (unsigned long)hs_total, (unsigned long)hs_timeout, (unsigned long)hs_garbage,
                        (unsigned)(uint8_t)(transaction_id ^ NUM_TOTAL_TRANSACTIONS),
                        (unsigned long)serial_debug_rx_fifo_level(),
                        (unsigned long)serial_debug_poll_hits(), (unsigned long)serial_debug_poll_miss(),
                        (unsigned long)serial_debug_poll_max_us(),
                        (unsigned long)serial_debug_irq_entries(), (unsigned long)serial_debug_irq_rxne());
            }
        }
#endif
        return false;
    }
#ifdef POLY_HANDSHAKE_DIAG
    // split42 diagnostic: on the SUCCESS path, periodically report the same counters so a
    // working link can be compared against a failing one (esp. irq_rxne + poll stats).
    {
        static uint32_t hs_ok = 0;
        if ((++hs_ok % 2000) == 0) {
            uprintf("HS-OK: ok=%lu irq_entries=%lu irq_rxne=%lu poll_hits=%lu poll_miss=%lu poll_max_us=%lu\n",
                    (unsigned long)hs_ok,
                    (unsigned long)serial_debug_irq_entries(), (unsigned long)serial_debug_irq_rxne(),
                    (unsigned long)serial_debug_poll_hits(), (unsigned long)serial_debug_poll_miss(),
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
