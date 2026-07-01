// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split_fw_up.h"

#include "quantum.h"
#include "polymod_crc32.h"
#include "base/fw_staging.h"   // fw_staging_set_fontpack_slot
#include "base/fontpack.h"     // fontpack_slot, FW_TARGET_FONTPACK via fw_staging.h

#include <transactions.h>
#include <print.h>
#include <string.h>

// ---------------------------------------------------------------------------
// HID firmware update — master-side chunk relay
// ---------------------------------------------------------------------------

// Relay one upload chunk to the slave with up to 10 retries. Builds the CRC'd
// fw_up_chunk_sync_t and sends USER_SYNC_FW_UP_CHUNK, accepting only an
// identity-bound reply whose write cursor advanced PAST `offset` (so a stale
// previous-chunk ACK can't silently desynchronise the two halves' cursors).
// Returns true on a slave ACK. Shared by the firmware-update and font-pack
// chunk paths, which used to carry a byte-identical copy of this loop. When
// `log_tag` is non-NULL and debug is enabled it logs each retry (the
// firmware-update path passes "FW_UP_CHUNK"); the font-pack path passes NULL
// for a quiet relay, matching its prior behaviour.
bool fw_up_relay_chunk_to_slave(uint32_t offset, const uint8_t *chunk_data, const char *log_tag) {
    fw_up_chunk_sync_t chunk_msg;
    chunk_msg.offset = offset;
    memcpy(chunk_msg.data, chunk_data, FW_UP_CHUNK_SIZE);
    chunk_msg.crc32 = crc32_1byte(&((const uint8_t *)&chunk_msg)[4], sizeof(chunk_msg) - 4, 0);
    uint8_t slave_ack = SYNC_CRC32_ERR;
    for (uint8_t retry = 0; retry < 10; ++retry) {
        fw_up_chunk_reply_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.ack = SYNC_CRC32_ERR;
        bool ok_rpc = transaction_rpc_exec(USER_SYNC_FW_UP_CHUNK, sizeof(chunk_msg), &chunk_msg,
                                           sizeof(reply), &reply);
        if (ok_rpc && reply.ack == SYNC_ACK && reply.next_offset > offset) {
            slave_ack = SYNC_ACK;
            if (log_tag && debug_enable && retry > 0) {
                uprintf("%s relay ok on retry %u (offset=%lu slave_next=%lu)\n",
                        log_tag, retry, offset, reply.next_offset);
            }
            break;
        }
        if (log_tag && debug_enable) {
            uprintf("%s relay retry %u (offset=%lu success=%d ack=0x%02x slave_next=%lu)\n",
                    log_tag, retry, offset, (int)ok_rpc, reply.ack, reply.next_offset);
        }
    }
    return slave_ack == SYNC_ACK;
}

// ---------------------------------------------------------------------------
// HID firmware update — slave-side split transaction handlers
// ---------------------------------------------------------------------------

// Boot-time version query.  Slave fills out_data with its own version+size.
void user_sync_fw_up_query_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(fw_up_query_sync_t) || !in_data || out_len != sizeof(fw_up_query_sync_t) || !out_data) return;
    fw_up_query_sync_t *reply = (fw_up_query_sync_t *)out_data;
    memset(reply, 0, sizeof(*reply));
    strncpy(reply->version, FW_VERSION, FW_UP_VERSION_LEN - 1);
    reply->fw_size = fw_staging_get_own_fw_size();
    reply->crc32   = crc32_1byte(reply->version, sizeof(*reply) - 4, 0);
}

// Diagnostic status query: slave returns its fw_staging internal counters
// + state so the master can uprintf them after a failed chunk to distinguish
// "slave hung before handler" vs "slave handler ran and rejected".  See
// FW_UP_DEBUG_NOTES.md.
void user_sync_fw_up_status_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (out_len != sizeof(fw_up_status_reply_t) || !out_data) return;
    // The request payload only exists to give the framework something to send;
    // CRC verification is best-effort — we still want to return a status snapshot
    // even if the request CRC is corrupt, since the CRC will most likely fail
    // exactly when something is wrong on the link.
    fw_up_status_reply_t *reply = (fw_up_status_reply_t *)out_data;
    fw_staging_get_status(&reply->status);
    reply->crc32 = crc32_1byte((const uint8_t *)&reply->status, sizeof(reply->status), 0);
    (void)in_len;
    (void)in_data;
}

// Slave schedules staging erase and reports readiness back to master.
//
// Protocol:
//   New image   : start deferred erase, return SYNC_ACK_SIG ("erase started, not ready").
//   Re-poll     : SYNC_CRC32_ERR while erasing, SYNC_ACK once complete.
//                 s_begun_size/crc are NOT reset on "done" — additional polls of the
//                 same image return SYNC_ACK without re-triggering the erase, so a
//                 UART-missed ACK does not cause an unnecessary full re-erase cycle.
//   Dirty retry : if chunks were written since the last erase (partial previous attempt),
//                 re-erase before allowing more writes (NOR flash needs erase before 0→1).
//
// The deferred erase is rate-limited to one sector per 70 ms in housekeeping so
// the split UART stays responsive between 50 ms erase blackouts.
void user_sync_fw_up_begin_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(fw_up_begin_sync_t) || !in_data || out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    fw_staging_note_begin_call();
    const fw_up_begin_sync_t *msg = (const fw_up_begin_sync_t *)in_data;
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    if (crc32 != msg->crc32) {
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }

    static uint32_t s_begun_size   = 0;
    static uint32_t s_begun_crc    = 0;
    static uint8_t  s_begun_target = 0xFF;
    static uint8_t  s_begun_bundle = 0xFF;
    bool same_image = (msg->image_size == s_begun_size && msg->image_crc == s_begun_crc &&
                       msg->target == s_begun_target && msg->bundle == s_begun_bundle);

    if (!same_image) {
        s_begun_size   = msg->image_size;
        s_begun_crc    = msg->image_crc;
        s_begun_target = msg->target;
        s_begun_bundle = msg->bundle;
        // FONTPACK: point the stager at this bundle's fixed slot before erasing.
        if (msg->target == FW_TARGET_FONTPACK) {
            uint32_t slot_off = 0, slot_size = 0;
            if (fontpack_slot(msg->bundle, &slot_off, &slot_size)) {
                fw_staging_set_fontpack_slot(slot_off, slot_size);
            } else {
                // Unknown bundle id — NACK so we never stage to a stale slot (the
                // master guards the same way with its slot_ok check).
                ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
                return;
            }
        }
        fw_staging_begin_deferred_target(msg->image_size, msg->image_crc, msg->target);
        uprintf("slave FW_UP_BEGIN: size=%lu crc=0x%08lx target=%u started erase\n",
                msg->image_size, msg->image_crc, msg->target);
        ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK_SIG;  // "erase started, keep polling"
        return;
    }

    // Re-poll: same image.
    if (fw_staging_erase_pending()) {
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }

    // Erase done.  If any chunks were written since the last erase (retry after a
    // partial failure), re-erase before handing off — NOR flash bits cannot go
    // 0→1 without erasing first.
    if (fw_staging_written()) {
        fw_staging_begin_deferred_target(msg->image_size, msg->image_crc, msg->target);
        uprintf("slave FW_UP_BEGIN: re-erasing dirty staging\n");
        ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK_SIG;
        return;
    }

    // Staging is clean and erase is complete.  Keep s_begun_size/crc set so
    // subsequent polls of the same image also return SYNC_ACK without restarting.
    uprintf("slave FW_UP_BEGIN: erase complete, ready\n");
    ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK;
}

// Slave writes one firmware chunk to its staging area.
// Replies with the identity-bound fw_up_chunk_reply_t (ack + post-RPC write
// cursor) so the master can tell a genuine ACK from a stale previous reply —
// see the struct comment in split_fw_up.h.
void user_sync_fw_up_chunk_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(fw_up_chunk_sync_t) || !in_data || out_len != sizeof(fw_up_chunk_reply_t) || !out_data) return;
    const fw_up_chunk_sync_t *msg = (const fw_up_chunk_sync_t *)in_data;
    fw_up_chunk_reply_t *reply = (fw_up_chunk_reply_t *)out_data;
    memset(reply, 0, sizeof(*reply));
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    uint8_t ack;
    if (crc32 != msg->crc32) {
        uprintf("slave FW_UP_CHUNK: CRC32 err offset=%lu\n", msg->offset);
        ack = SYNC_CRC32_ERR;
    } else {
        // Step 1: slave actually writes the chunk into its staging buffer.
        // 56 B per chunk → s_page_buf; every FLASH_PAGE_SIZE/56 ≈ 5 chunks
        // the buffer fills and flush_page() halts core1 via PSM, disables
        // IRQs, runs flash_range_program (~5 ms), restores IRQs, restarts
        // core1.  ~1100 page writes over a full fw_up.  If any of these
        // wedges the next chunk RPC, the master log will stop logging
        // chunks and `slave status (chunk-fail)` will show whether the
        // slave is fully hung or just slow.
#ifdef FW_UP_CHUNK_NOOP_PROBE
        // PROBE (run 4): skip ONLY the staging write — keep the in_len guard and
        // the CRC check above (the 2026-05-20 "verified transport baseline"
        // b4a939f6 did exactly this and streamed every chunk).  The run-3/4
        // bracket probe proved a same-size 64 B FW_UP_STATUS txn round-trips on
        // the slave microseconds before this chunk, so the only change from that
        // streaming baseline is removing fw_staging_write_chunk:
        //   chunks ACK now    -> the wedge is inside fw_staging_write_chunk
        //   chunks still fail  -> the FW_UP_CHUNK transaction dispatch itself
        //                         wedges (or the slave isn't running this build)
        // See config.h FW_UP_CHUNK_NOOP_PROBE / FW_UP_BASELINE.md (run 4).
        bool ok = true;
#else
        bool ok = fw_staging_write_chunk(msg->offset, msg->data, FW_UP_CHUNK_SIZE);
#endif
        ack = ok ? SYNC_ACK : SYNC_CRC32_ERR;
    }
    fw_staging_note_chunk_call(msg->offset, ack);
    reply->ack         = ack;
    reply->next_offset = fw_staging_next_offset();
}

// Slave verifies staged CRC and arms the commit flag.
// The actual flash apply is deferred to housekeeping_task_user() so the ACK
// can be returned before the split link goes dark during the reboot.
void user_sync_fw_up_commit_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    // Defer the heavy FONTPACK reload out of this transaction callback: the
    // ~50 ms full-body verify+reassemble overran the ~20 ms split-transaction
    // window, so the master timed out and mis-reported COMMIT as a CRC failure
    // even though the pack loaded. Firmware-target finalize is O(1) (unaffected).
    bool ok = fw_staging_finalize_defer_reload();
    ((poly_sync_reply_t *)out_data)->ack = ok ? SYNC_ACK : SYNC_CRC32_ERR;
}

// Master commands the slave to install its staged image and reboot, so BOTH
// halves restart together (like a replug).  Without this the slave keeps running
// its old firmware in a stale state, and the rebooted master cannot re-establish
// the split link — it hangs on the boot splash (the right half's "SPLIT 72").
// Guarded by a magic + CRC and a valid-staged-image check so a stray transaction
// can never trigger an apply.  The apply is deferred to the slave's housekeeping
// so we ACK the master before our split link goes dark.
void user_sync_fw_up_apply_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(fw_up_apply_sync_t) || !in_data || out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    const fw_up_apply_sync_t *msg = (const fw_up_apply_sync_t *)in_data;
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    if (crc32 != msg->crc32 || msg->magic != FW_UP_SYNC_MAGIC || !fw_staging_has_valid_staged_image()) {
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }
    fw_staging_arm_apply();   // housekeeping_task_user() → fw_staging_apply_and_reboot()
    ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK;
}

// Master commands the slave to reboot (QK_REBOOT path — no firmware apply).
// Same rationale as the apply handler: a master-only reset leaves the slave
// stale and the master stuck on the splash; rebooting both mirrors a replug.
// Deferred to the slave's housekeeping so we ACK first, then reset cleanly.
void user_sync_reboot_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(fw_up_apply_sync_t) || !in_data || out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    const fw_up_apply_sync_t *msg = (const fw_up_apply_sync_t *)in_data;
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    if (crc32 != msg->crc32 || msg->magic != FW_UP_SYNC_MAGIC) {
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }
    // Handedness-change carrier (see fw_up_apply_sync_t): when requested, persist
    // this (slave) half's new EE_HANDS marker before the reboot so it comes up on
    // the corrected left/right assignment.  Plain QK_REBOOT leaves this zero.
    if (msg->set_handedness) {
        eeconfig_update_handedness(msg->is_left != 0);
    }
    fw_staging_arm_reboot();   // housekeeping_task_user() → mcu_reset()
    ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK;
}
