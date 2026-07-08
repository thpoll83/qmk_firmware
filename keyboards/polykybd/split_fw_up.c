// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split_fw_up.h"

#include "quantum.h"
#include "polymod_crc32.h"
#include "base/fw_staging.h"   // fw_staging_set_fontpack_slot
#include "base/fontpack.h"     // fontpack_slot, FW_TARGET_FONTPACK via fw_staging.h

#include <transactions.h>
#include "split_common/split_util.h"   // is_transport_connected()
#include <print.h>
#include <string.h>

// ---------------------------------------------------------------------------
// HID firmware update — master-side chunk relay
// ---------------------------------------------------------------------------

// Relay one upload chunk to the slave with up to 10 retries. Builds the CRC'd
// fw_up_chunk_sync_t (op = FLASH_STAGE_CHUNK) and sends USER_SYNC_FLASH_STAGE,
// accepting only an identity-bound reply whose write cursor advanced PAST `offset`
// (so a stale previous-chunk ACK can't silently desynchronise the two halves'
// cursors). Returns true on a slave ACK. Shared by the firmware-update and
// font-pack chunk paths, which used to carry a byte-identical copy of this loop.
// When `log_tag` is non-NULL and debug is enabled it logs each retry (the
// firmware-update path passes "FW_UP_CHUNK"); the font-pack path passes NULL
// for a quiet relay, matching its prior behaviour.
bool fw_up_relay_chunk_to_slave(uint32_t offset, const uint8_t *chunk_data, const char *log_tag) {
    fw_up_chunk_sync_t chunk_msg;
    chunk_msg.op     = FLASH_STAGE_CHUNK;
    chunk_msg.offset = offset;
    memcpy(chunk_msg.data, chunk_data, FW_UP_CHUNK_SIZE);
    chunk_msg.crc32 = crc32_1byte(&((const uint8_t *)&chunk_msg)[4], sizeof(chunk_msg) - 4, 0);
    uint8_t slave_ack = SYNC_CRC32_ERR;
    for (uint8_t retry = 0; retry < 10; ++retry) {
        fw_up_chunk_reply_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.ack = SYNC_CRC32_ERR;
        bool ok_rpc = transaction_rpc_exec(USER_SYNC_FLASH_STAGE, sizeof(chunk_msg), &chunk_msg,
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

// See the header: false on a solo half (no slave), so the flash paths flash the
// master's own copy without waiting on a slave that isn't there.
bool fw_up_slave_present(void) {
    return is_transport_connected();
}

// ---------------------------------------------------------------------------
// HID firmware update — slave-side split transaction handlers
// ---------------------------------------------------------------------------

// Diagnostic status query: slave returns its fw_staging internal counters
// + state so the master can uprintf them after a failed chunk to distinguish
// "slave hung before handler" vs "slave handler ran and rejected".  See
// FW_UP_DEBUG_NOTES.md.
static void flash_stage_status(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
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
static void flash_stage_begin(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
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
        } else if (msg->target == FW_TARGET_DOOMWAD) {
            // The doom WHX slot is fixed (upper resource region).
            fw_staging_set_fontpack_slot(FW_DOOMWAD_SLOT_OFF, FW_DOOMWAD_SLOT_SIZE);
        } else if (msg->target == FW_TARGET_DOOMPACK) {
            // The executable engine pack slot is fixed (top 256 KB —
            // doom/PACK_DESIGN.md; the slave's drone runs the same pack).
            fw_staging_set_fontpack_slot(FW_DOOMPACK_SLOT_OFF, FW_DOOMPACK_SLOT_SIZE);
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
static void flash_stage_chunk(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
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
static void flash_stage_commit(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(fw_up_commit_sync_t) || !in_data || out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    // Verify the request CRC (covers the `op` selector) so COMMIT carries the same
    // size + integrity guard as BEGIN/CHUNK — send_to_bridge always fills a valid
    // CRC, so this only rejects a corrupted/misrouted frame, which the master retries.
    const fw_up_commit_sync_t *msg = (const fw_up_commit_sync_t *)in_data;
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    if (crc32 != msg->crc32) {
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }
    // Defer the heavy FONTPACK reload out of this transaction callback: the
    // ~50 ms full-body verify+reassemble overran the ~20 ms split-transaction
    // window, so the master timed out and mis-reported COMMIT as a CRC failure
    // even though the pack loaded. Firmware-target finalize is O(1) (unaffected).
    bool ok = fw_staging_finalize_defer_reload();
    ((poly_sync_reply_t *)out_data)->ack = ok ? SYNC_ACK : SYNC_CRC32_ERR;
}

// Single slave-side dispatcher for the flash-staging stream.  Reads the `op`
// word (immediately after the CRC32) and routes to the per-op handler above.
// BEGIN / CHUNK / COMMIT each verify the request CRC32 (which covers `op`) and
// guard their exact in_len/out_len — so a corrupted or misrouted op lands on a
// size/CRC mismatch and the handler returns without a valid ACK, which the master
// reads as a failure and retries.  STATUS is intentionally best-effort: un-CRC'd
// and guarding only on out_len (so it can still answer the deliberately-oversized
// diagnostic probe) — it is read-only, so a misroute to it is harmless.
void user_sync_flash_stage_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (!in_data || in_len < 8) return;   // need at least crc32 + op
    uint32_t op;
    memcpy(&op, &((const uint8_t *)in_data)[4], sizeof(op));
    switch (op) {
        case FLASH_STAGE_BEGIN:  flash_stage_begin (in_len, in_data, out_len, out_data); break;
        case FLASH_STAGE_CHUNK:  flash_stage_chunk (in_len, in_data, out_len, out_data); break;
        case FLASH_STAGE_COMMIT: flash_stage_commit(in_len, in_data, out_len, out_data); break;
        case FLASH_STAGE_STATUS: flash_stage_status(in_len, in_data, out_len, out_data); break;
        default: break;   // unknown op — leave out_data untouched → master retries
    }
}

// Master commands the slave to restart, so BOTH halves come back together (like a
// replug).  Without this the slave keeps running its old firmware / stale state,
// and the rebooted master cannot re-establish the split link — it hangs on the
// boot splash (the right half's "SPLIT 72").  ONE transaction (USER_SYNC_RESET)
// serves every restart path; the `action` byte selects apply-and-reboot vs plain
// reboot.  Guarded by a magic + CRC (and, for apply, a valid-staged-image check)
// so a stray transaction can never trigger an unexpected reset.  The restart is
// deferred to the slave's housekeeping so we ACK the master before our split link
// goes dark.
void user_sync_reset_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(poly_reset_sync_t) || !in_data || out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    const poly_reset_sync_t *msg = (const poly_reset_sync_t *)in_data;
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    if (crc32 != msg->crc32 || msg->magic != POLY_RESET_MAGIC) {
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }
    if (msg->action == RESET_ACTION_APPLY) {
        // Install the staged image, then reboot — reject unless a valid image is staged.
        if (!fw_staging_has_valid_staged_image()) {
            ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
            return;
        }
        fw_staging_arm_apply();    // housekeeping_task_user() → fw_staging_apply_and_reboot()
    } else if (msg->action == RESET_ACTION_REBOOT) {
        // Reboot only (QK_REBOOT path). Handedness-change carrier (see
        // poly_reset_sync_t): when requested, persist this (slave) half's new
        // EE_HANDS marker before the reboot so it comes up on the corrected
        // left/right assignment.  Plain QK_REBOOT leaves this zero.
        if (msg->set_handedness) {
            eeconfig_update_handedness(msg->is_left != 0);
        }
        fw_staging_arm_reboot();   // housekeeping_task_user() → mcu_reset()
    } else {
        // Unknown action — refuse rather than guess (magic+CRC already passed, so
        // this only happens on a genuinely malformed request).
        ((poly_sync_reply_t *)out_data)->ack = SYNC_CRC32_ERR;
        return;
    }
    ((poly_sync_reply_t *)out_data)->ack = SYNC_ACK;
}
