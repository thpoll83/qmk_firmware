// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split_fw_up.h"

#include "quantum.h"
#include "polymod_crc32.h"

#include <string.h>

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

    static uint32_t s_begun_size = 0;
    static uint32_t s_begun_crc  = 0;
    bool same_image = (msg->image_size == s_begun_size && msg->image_crc == s_begun_crc);

    if (!same_image) {
        s_begun_size = msg->image_size;
        s_begun_crc  = msg->image_crc;
        fw_staging_begin_deferred(msg->image_size, msg->image_crc);
        uprintf("slave FW_UP_BEGIN: size=%lu crc=0x%08lx started erase\n", msg->image_size, msg->image_crc);
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
        fw_staging_begin_deferred(msg->image_size, msg->image_crc);
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
void user_sync_fw_up_chunk_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len != sizeof(fw_up_chunk_sync_t) || !in_data || out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    const fw_up_chunk_sync_t *msg = (const fw_up_chunk_sync_t *)in_data;
    uint32_t crc32 = crc32_1byte(&((const uint8_t *)in_data)[4], in_len - 4, 0);
    uint8_t ack;
    if (crc32 != msg->crc32) {
        uprintf("slave FW_UP_CHUNK: CRC32 err offset=%lu\n", msg->offset);
        ack = SYNC_CRC32_ERR;
    } else {
        // BASELINE: slave dummy-accepts the chunk — does NOT write to staging.
        // The slave's fw_staging_write_chunk path needs to be re-added next;
        // see FW_UP_BASELINE.md for the bisection plan.
        uprintf("slave FW_UP_CHUNK: offset=%lu (dummy accept)\n", msg->offset);
        ack = SYNC_ACK;
    }
    fw_staging_note_chunk_call(msg->offset, ack);
    ((poly_sync_reply_t *)out_data)->ack = ack;
}

// Slave verifies staged CRC and arms the commit flag.
// The actual flash apply is deferred to housekeeping_task_user() so the ACK
// can be returned before the split link goes dark during the reboot.
void user_sync_fw_up_commit_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (out_len != sizeof(poly_sync_reply_t) || !out_data) return;
    bool ok = fw_staging_finalize();
    ((poly_sync_reply_t *)out_data)->ack = ok ? SYNC_ACK : SYNC_CRC32_ERR;
}
