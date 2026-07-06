// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bridge_helper.h"

#include QMK_KEYBOARD_H
#include "quantum.h"
#include "polymod_crc32.h"
#include "split_sync.h"
#include "config.h"

#include <print.h>
#include <transactions.h>

#include <string.h>

static enum com_state com = NOT_INITIALIZED;

// ── Split-link quality counters (master side) ───────────────────────────────
// Each transaction_rpc_exec() call is one frame on the split UART link. The
// QMK vendor transport carries the payload with no integrity check of its own
// (only a 1-byte handshake token + a 20 ms timeout), so the per-transaction
// CRC32 in split_sync.c is the only thing that detects a bit flipped in flight.
// Counting the outcomes here lets us measure the link's real error behaviour
// (the bit-error rate `p`) and quantitatively compare cable / baud / drive
// changes instead of guessing. Cumulative since boot; a compact summary is
// emitted from send_to_bridge() every LINK_STATS_LOG_EVERY frames — count-based,
// so the cadence follows real traffic (dense during overlay bursts, sparse when
// idle, silent with no traffic) without needing a timer. Touched only on core0
// (the master's main loop) — no ISR access, so plain (non-volatile) is fine.
#ifndef LINK_STATS_LOG_EVERY
#    define LINK_STATS_LOG_EVERY 200   // emit one health line per N frames sent
#endif
static uint32_t ls_attempts       = 0;  // frames sent (transaction_rpc_exec calls)
static uint32_t ls_crc_err        = 0;  // slave NACK'd / non-ACK reply → payload corrupted in flight
static uint32_t ls_transport_fail = 0;  // transport returned false (timeout / handshake / no reply)
static uint32_t ls_call_giveup    = 0;  // send_to_bridge() calls that exhausted every retry
static uint32_t ls_last_log       = 0;  // ls_attempts at the last emitted summary

void set_com_state(enum com_state state) {
    com = state;
}

bool is_usb_host_side(void) {
    return com == USB_HOST;
}

const char* tid_to_str(int8_t tid) {
    switch (tid)
    {
    case USER_SYNC_LAYER_DATA: return "UserLayer";
    case USER_SYNC_POLY_DATA:  return "UserPoly";
    case USER_SYNC_LASTKEY_DATA: return "UserLastKey";
    case USER_SYNC_LATIN_EX_DATA: return "UserLatinEx";
    case USER_SYNC_OVERLAY_DATA: return "UserOverlay";
    case USER_SYNC_COMPRESSED_DATA: return "UserCompressed";
    case USER_SYNC_ROI_DATA: return "UserRoi";
    case USER_SYNC_DYNAMIC_KEYMAP_DATA: return "UserDynMap";
    case USER_SYNC_OVERLAY_MAP_DATA: return "UserOverlayMap";
    case USER_SYNC_FW_UP_BEGIN:  return "FwUpBegin";
    case USER_SYNC_FW_UP_CHUNK:  return "FwUpChunk";
    case USER_SYNC_FW_UP_COMMIT: return "FwUpCommit";
    case USER_SYNC_FW_UP_STATUS: return "FwUpStatus";
    case USER_SYNC_RESET:        return "Reset";
    default: return "Not registered";
    }
}

uint8_t send_to_bridge(int8_t tid, void* buffer_with4crc_bytes, const uint8_t num_bytes, const uint8_t max_retries) {
    poly_sync_reply_t reply;
    uint8_t retry = 0;

    // Periodic split-link health line, once per LINK_STATS_LOG_EVERY frames sent.
    // Count-based (no timer): the cadence tracks real traffic. Checked once per
    // call on fully-settled cumulative counts, so it covers every exit path.
    // Emitted unconditionally (uprintf, not debug_enable-gated): this is a passive
    // wire-health diagnostic with no key content, so it stays available even with
    // debug off (debug_enable now defaults false to suppress keystroke logging).
    if ((ls_attempts - ls_last_log) >= LINK_STATS_LOG_EVERY) {
        ls_last_log = ls_attempts;
        uint32_t errs    = ls_crc_err + ls_transport_fail;
        uint32_t permille = ls_attempts ? (uint32_t)(((uint64_t)errs * 1000U) / ls_attempts) : 0U;
        uprintf("Split link: %lu tx crc_err=%lu transport_fail=%lu giveup=%lu err=%lu.%lu%%\n",
                (unsigned long)ls_attempts, (unsigned long)ls_crc_err,
                (unsigned long)ls_transport_fail, (unsigned long)ls_call_giveup,
                (unsigned long)(permille / 10U), (unsigned long)(permille % 10U));
    }

    *((uint32_t *)buffer_with4crc_bytes) = crc32_1byte(&((uint8_t *)buffer_with4crc_bytes)[4], num_bytes-4, 0);
    for(; retry<max_retries; ++retry) {
        // Reset the reply each iteration: transaction_rpc_exec() leaves it
        // untouched when transport_write/read fails, so without this the log
        // line below would print the previous successful call's ack value.
        reply.ack = SYNC_CRC32_ERR;
        bool sync_success = transaction_rpc_exec(tid, num_bytes, buffer_with4crc_bytes, sizeof(poly_sync_reply_t), &reply);
        ls_attempts++;
        if(sync_success && (reply.ack == SYNC_ACK || reply.ack == SYNC_ACK_SIG)) {
            // A recovered retry is a non-event — the LINK_STATS summary already
            // counts retries/errors. Announcing every success spammed the console
            // unusably during a font-pack flash (thousands of bridged frames).
            return reply.ack;
        }
        // Failed attempt. sync_success==true means the transport delivered a
        // reply but it wasn't an ACK → the slave rejected the frame on CRC (or
        // the reply itself was corrupted): a payload-integrity miss. false means
        // the transport gave up (timeout / bad handshake / no reply).
        if(sync_success) {
            ls_crc_err++;
        } else {
            ls_transport_fail++;
        }
        if(debug_enable) {
            uprintf("Bridge sync retry %d (tid: %s, success: %d, ack: %d, bytes: %d)\n", retry, tid_to_str(tid), sync_success, reply.ack, num_bytes);
        }
    }
    ls_call_giveup++;
    if(debug_enable) {
        uprintf("Failed to sync %d bytes for transaction %s!\n", num_bytes, tid_to_str(tid));
    }

    return SYNC_CRC32_ERR;
}

bool differ(const void* b1, const void* b2, uint8_t byte_count) {
    return memcmp(&((const uint8_t *)b1)[4], &((const uint8_t *)b2)[4], byte_count - 4) != 0; // start after crc32
}
