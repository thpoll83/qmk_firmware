// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "serial.h"
#include "serial_protocol.h"
#include "printf.h"
#include "synchronization_util.h"

#include <ch.h>

static const uint8_t HANDSHAKE_MAGIC = 7U;

static inline bool initiate_transaction(uint8_t sstd_index);
static inline bool react_to_transactions(void);

/**
 * @brief This thread runs on the slave and responds to transactions initiated
 * by the master.
 */
static THD_WORKING_AREA(waSlaveThread, 1024);
static THD_FUNCTION(SlaveThread, arg) {
    (void)arg;
    chRegSetThreadName("usart_tx_rx");

    while (true) {
        if (unlikely(!react_to_transactions())) {
            /* Clear the receive queue, to start with a clean slate.
             * Parts of failed transactions or spurious bytes could still be in it. */
            driver_clear();
        }
        split_shared_memory_unlock();
    }
}

/**
 * @brief Slave specific initializations.
 */
void soft_serial_target_init(void) {
    driver_slave_init();

    /* Start transport thread. */
    chThdCreateStatic(waSlaveThread, sizeof(waSlaveThread), HIGHPRIO, SlaveThread, NULL);
}

/**
 * @brief Master specific initializations.
 */
void soft_serial_initiator_init(void) {
    driver_master_init();
}

/**
 * @brief React to transactions started by the master.
 */
static inline bool react_to_transactions(void) {
    /* Wait until there is a transaction for us. */
    uint8_t sstd_index = 0;
    receive_blocking(&sstd_index, sizeof(sstd_index));

    /* Sanity check that we are actually responding to a valid transaction. */
    if (unlikely(sstd_index >= NUM_TOTAL_TRANSACTIONS)) {
        return false;
    }

    split_shared_memory_lock();
    split_transaction_desc_t* trans = &split_transaction_table[sstd_index];

    /* Send back the handshake which is XORed as a simple checksum,
     to signal that the slave is ready to receive possible transaction buffers  */
    sstd_index ^= HANDSHAKE_MAGIC;
    if (unlikely(!send(&sstd_index, sizeof(sstd_index)))) {
        return false;
    }

    /* Receive transaction buffer from the master. If this transaction requires it.*/
    if (trans->initiator2target_buffer_size) {
        if (unlikely(!receive(split_trans_initiator2target_buffer(trans), trans->initiator2target_buffer_size))) {
            return false;
        }
    }

    /* Allow any slave processing to occur. */
    if (trans->slave_callback) {
        trans->slave_callback(trans->initiator2target_buffer_size, split_trans_initiator2target_buffer(trans), trans->initiator2target_buffer_size, split_trans_target2initiator_buffer(trans));
    }

    /* Send transaction buffer to the master. If this transaction requires it. */
    if (trans->target2initiator_buffer_size) {
        if (unlikely(!send(split_trans_target2initiator_buffer(trans), trans->target2initiator_buffer_size))) {
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
    split_shared_memory_lock();
    bool result = initiate_transaction((uint8_t)index);
    split_shared_memory_unlock();

    if (unlikely(!result)) {
        /* Clear the receive queue, to start with a clean slate.
         * Parts of failed transactions or spurious bytes could still be in it. */
        driver_clear();
    }

    return result;
}

/**
 * @brief Initiate transaction to slave half.
 */
static inline bool initiate_transaction(uint8_t sstd_index) {
    /* Sanity check that we are actually starting a valid transaction. */
    if (unlikely(sstd_index >= NUM_TOTAL_TRANSACTIONS)) {
        dprintln("USART: Illegal transaction Id.");
        return false;
    }

    split_transaction_desc_t* trans = &split_transaction_table[sstd_index];

    /* Send transaction table index to the slave, which doubles as basic handshake token. */
    if (unlikely(!send(&sstd_index, sizeof(sstd_index)))) {
        dprintln("USART: Send Handshake failed.");
        return false;
    }

    uint8_t sstd_index_shake = 0xFF;

    /* Which we always read back first so that we can error out correctly.
     *   - due to the half duplex limitations on return codes, we always have to read *something*.
     *   - without the read, write only transactions *always* succeed, even during the boot process where the slave is not ready.
     */
    if (unlikely(!receive(&sstd_index_shake, sizeof(sstd_index_shake)) || (sstd_index_shake != (sstd_index ^ HANDSHAKE_MAGIC)))) {
        dprintln("USART: Handshake failed.");
        return false;
    }

    /* Send transaction buffer to the slave. If this transaction requires it. */
    if (trans->initiator2target_buffer_size) {
        if (unlikely(!send(split_trans_initiator2target_buffer(trans), trans->initiator2target_buffer_size))) {
            dprintln("USART: Send failed.");
            return false;
        }
    }

    /* Receive transaction buffer from the slave. If this transaction requires it. */
    if (trans->target2initiator_buffer_size) {
        if (unlikely(!receive(split_trans_target2initiator_buffer(trans), trans->target2initiator_buffer_size))) {
            dprintln("USART: Receive failed.");
            return false;
        }
    }

    return true;
}
