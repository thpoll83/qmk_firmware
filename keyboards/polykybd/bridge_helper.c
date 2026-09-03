// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bridge_helper.h"

#include QMK_KEYBOARD_H
#include "quantum.h"
#include "polymod_crc32.h"
#include "split_sync.h"
#include "config.h"
#include "profiling/loop_profile.h"
#include "base/crash_record.h"   // crash_phase_enter()/leave() around the blocking bridge
#ifdef POLYKYBD_LOOP_PROFILE
#    include "hardware/structs/timer.h"   // timer_hw->timerawl — raw 1 MHz us counter
#endif

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
static uint32_t ls_crc_err        = 0;  // reply was SYNC_CRC32_ERR → payload corrupted in flight
static uint32_t ls_nack           = 0;  // reply was a valid non-ACK (BUSY / REFUSED) → NOT a link fault
static uint32_t ls_transport_fail = 0;  // transport returned false (timeout / handshake / no reply)
static uint32_t ls_call_giveup    = 0;  // calls that exhausted every retry on a LINK fault
                                        // (not counted when the slave answered BUSY/REFUSED —
                                        //  see the note where it is incremented)
static uint32_t ls_last_log       = 0;  // ls_attempts at the last emitted summary

void poly_get_link_stats(poly_link_stats_t* out) {
    if (!out) return;
    out->attempts       = ls_attempts;
    out->crc_err        = ls_crc_err;
    out->nack           = ls_nack;
    out->transport_fail = ls_transport_fail;
    out->giveup         = ls_call_giveup;
}

uint32_t poly_link_err_permille(void) {
    // Same expression as the periodic console summary above — kept here rather than
    // duplicated at the caller so the panel and the log can never diverge.
    const uint32_t errs = ls_crc_err + ls_transport_fail;
    return ls_attempts ? (uint32_t)(((uint64_t)errs * 1000U) / ls_attempts) : 0U;
}

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
    case USER_SYNC_FLASH_STAGE:  return "FlashStage";
    case USER_SYNC_RESET:        return "Reset";
    default: return "Not registered";
    }
}

static uint8_t send_to_bridge_impl(int8_t tid, void* buffer_with4crc_bytes, const uint8_t num_bytes, const uint8_t max_retries);

// Tags the crash-record phase breadcrumb around the blocking bridge: a watchdog
// timeout inside a split transaction then names the transaction it was in.
uint8_t send_to_bridge(int8_t tid, void* buffer_with4crc_bytes, const uint8_t num_bytes, const uint8_t max_retries) {
    uint32_t tag = crash_phase_enter(CRASH_PHASE_BRIDGE, (uint16_t)(uint8_t)tid);
    uint8_t  ack = send_to_bridge_impl(tid, buffer_with4crc_bytes, num_bytes, max_retries);
    crash_phase_leave(tag);
    return ack;
}

static uint8_t send_to_bridge_impl(int8_t tid, void* buffer_with4crc_bytes, const uint8_t num_bytes, const uint8_t max_retries) {
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
        // err% counts only real LINK faults — a corrupted frame or no answer at
        // all. `nack` (a valid SYNC_BUSY / SYNC_NACK_REFUSED answer) is deliberately
        // excluded: the wire worked, the slave simply said something other than yes.
        // Counting those would inflate the one number used to judge cable / baud /
        // drive changes — and SYNC_BUSY arrives on EVERY erase re-poll of a flash,
        // so a font-pack update alone would have added hundreds of phantom "errors".
        uint32_t errs    = ls_crc_err + ls_transport_fail;
        uint32_t permille = ls_attempts ? (uint32_t)(((uint64_t)errs * 1000U) / ls_attempts) : 0U;
        uprintf("Split link: %lu tx crc_err=%lu nack=%lu transport_fail=%lu giveup=%lu err=%lu.%lu%%\n",
                (unsigned long)ls_attempts, (unsigned long)ls_crc_err, (unsigned long)ls_nack,
                (unsigned long)ls_transport_fail, (unsigned long)ls_call_giveup,
                (unsigned long)(permille / 10U), (unsigned long)(permille % 10U));
    }

    *((uint32_t *)buffer_with4crc_bytes) = crc32_1byte(&((uint8_t *)buffer_with4crc_bytes)[4], num_bytes-4, 0);
    // What the slave ACTUALLY said, kept across retries. Returning a constant on
    // give-up threw this away, which made every non-ACK answer indistinguishable
    // from silence — see the note at the return below.
    uint8_t last_ack  = SYNC_GIVEUP;
    bool    got_reply = false;
    for(; retry<max_retries; ++retry) {
        // Reset the reply each iteration: transaction_rpc_exec() leaves it
        // untouched when transport_write/read fails, so without this the log
        // line below would print the previous successful call's ack value.
        // SYNC_GIVEUP is the honest sentinel — "no answer obtained" — so a
        // transport failure no longer reports itself as a CRC error in the log.
        reply.ack = SYNC_GIVEUP;
#ifdef POLYKYBD_LOOP_PROFILE
        uint32_t _lp_t0 = timer_hw->timerawl;
#endif
        bool sync_success = transaction_rpc_exec(tid, num_bytes, buffer_with4crc_bytes, sizeof(poly_sync_reply_t), &reply);
#ifdef POLYKYBD_LOOP_PROFILE
        // Account the blocking transport time (this retry) against the current
        // main-loop iteration, so the profiler can attribute stalls to the bridge.
        loop_profile_add_bridge_us(timer_hw->timerawl - _lp_t0);
#endif
        ls_attempts++;
        if(sync_success) {
            // The transport delivered an answer. Remember it even if we go on to
            // retry, so the caller can still learn what the slave said.
            got_reply = true;
            last_ack  = reply.ack;
        }
        if(sync_success && (reply.ack == SYNC_ACK || reply.ack == SYNC_ACK_SIG)) {
            // A recovered retry is a non-event — the LINK_STATS summary already
            // counts retries/errors. Announcing every success spammed the console
            // unusably during a font-pack flash (thousands of bridged frames).
            return reply.ack;
        }
        // Failed attempt — but classify WHY, because only some of these are link
        // faults. A SYNC_CRC32_ERR reply is a payload-integrity miss (the frame
        // arrived corrupted). SYNC_BUSY / SYNC_NACK_REFUSED are valid protocol
        // answers over a perfectly good wire and must not be counted as errors.
        // sync_success==false means the transport gave up (timeout / bad
        // handshake / no reply).
        if(!sync_success) {
            ls_transport_fail++;
        } else if(sync_is_link_fault(true, reply.ack)) {
            ls_crc_err++;
        } else {
            ls_nack++;
        }
        if(debug_enable) {
            uprintf("Bridge sync retry %d (tid: %s, success: %d, ack: %d, bytes: %d)\n", retry, tid_to_str(tid), sync_success, reply.ack, num_bytes);
        }
    }
    // Count a give-up only when the call ended on a LINK fault. A call whose last
    // answer was a protocol verdict — SYNC_BUSY above all — did not give up on
    // anything: the slave answered, and the answer was simply not yes.
    //
    // This matters because it is the COMMON case, not an edge one. The
    // flash_stage_begin re-poll runs with max_retries=1, so EVERY poll of a
    // deferred erase used to land here: a healthy font-pack sync measured
    // `nack=11 transport_fail=1 giveup=12` on hardware (2026-08-18) — a link with
    // one real fault reporting twelve give-ups. `giveup` is read as "the link is
    // failing", so inflating it with normal polling is the same category error
    // that made err% read 6.0% on that link instead of 0.5%.
    if(sync_is_link_fault(got_reply, last_ack)) {
        ls_call_giveup++;
    }
    if(debug_enable) {
        uprintf("Failed to sync %d bytes for transaction %s!\n", num_bytes, tid_to_str(tid));
    }

    // Return what the slave SAID, and SYNC_GIVEUP only when it never answered.
    //
    // ⚠️ This used to return a constant, discarding reply.ack — and that silently
    // defeated the whole point of having distinct failure values. It worked before
    // only by COINCIDENCE: the constant was SYNC_CRC32_ERR, which happened to equal
    // what the slave sent in every case that mattered.
    //
    // fw_up_slave_refused_commit()'s "a refusal is self-describing, so don't spend
    // a STATUS RPC" short-circuit was DEAD CODE because of it: a refusal arrived
    // here as the give-up constant, never as SYNC_NACK_REFUSED, so every refusal
    // paid for a probe.
    //
    // hid_fw_up.c's erase-progress counter is the instructive near-miss — it kept
    // working, but not for the reason it reads as. Its guard accepts SYNC_BUSY
    // *or* SYNC_CRC32_ERR, a compat arm added for transiently-mismatched halves,
    // and that arm happened to match the give-up constant too. So a defensive
    // clause written for one hazard silently covered the discard, and the counter
    // fired on a value the slave had not sent. It now matches SYNC_BUSY because
    // the slave actually said SYNC_BUSY.
    // Callers that only ask sync_succeeded() are unaffected — it is a whitelist,
    // and none of these values is an ACK.
    return got_reply ? last_ack : SYNC_GIVEUP;
}

bool differ(const void* b1, const void* b2, uint8_t byte_count) {
    return memcmp(&((const uint8_t *)b1)[4], &((const uint8_t *)b2)[4], byte_count - 4) != 0; // start after crc32
}
