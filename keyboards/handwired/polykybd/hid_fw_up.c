// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hid_fw_up.h"

#include QMK_KEYBOARD_H
#include "raw_hid.h"
#include "config.h"
#include "split_fw_up.h"
#include "bridge_helper.h"
#include <transactions.h>
#include "base/fw_staging.h"
#include "hardware/flash.h"

#include <print.h>
#include <string.h>

// HID report byte offsets
#define HID_DATA_IDX 2

// Query the slave's fw_staging internal state and uprintf the result.
// Used on the master after a failed FW_UP_CHUNK (and once when the slave
// first reports ready) so we can tell from the master serial log whether
// the slave is alive at all, whether the chunk handler ran, and what its
// next_offset / counters look like.  See FW_UP_DEBUG_NOTES.md.
static void fw_up_log_slave_status(const char *tag) {
    fw_up_status_request_t req = { .crc32 = 0, .dummy = 0 };
    fw_up_status_reply_t   reply;
    memset(&reply, 0, sizeof(reply));
    // Use transaction_rpc_exec directly — send_to_bridge hardcodes the
    // 1-byte poly_sync_reply_t and can't carry our bigger status struct.
    bool ok = transaction_rpc_exec(USER_SYNC_FW_UP_STATUS,
                                   sizeof(req), &req,
                                   sizeof(reply), &reply);
    if (!ok) {
        uprintf("slave status (%s): RPC FAILED — slave unresponsive\n", tag);
        return;
    }
    const fw_staging_status_t *s = &reply.status;
    uprintf("slave status (%s): init=%u active=%u erase_pending=%u erase=%u/%u "
            "next_off=%lu begin_calls=%u chunk_calls=%u chunk_errs=%u "
            "last_chunk_off=%lu last_ack=0x%02x\n",
            tag,
            (unsigned)s->initialized, (unsigned)s->fw_up_active, (unsigned)s->erase_pending,
            (unsigned)s->erase_sector_next, (unsigned)s->erase_sector_count,
            (unsigned long)s->next_offset,
            (unsigned)s->begin_handler_calls, (unsigned)s->chunk_handler_calls,
            (unsigned)s->chunk_handler_errors,
            (unsigned long)s->last_chunk_offset, (unsigned)s->last_chunk_ack);
}

bool hid_fw_up_receive(uint8_t *data, uint8_t length) {
    switch (data[1]) {

        case CMD_FW_UP_BEGIN: { // data[2..5]=image_size, data[6..9]=image_crc
            uint32_t image_size, image_crc;
            memcpy(&image_size, &data[HID_DATA_IDX],   4);
            memcpy(&image_crc,  &data[HID_DATA_IDX+4], 4);
            bool master_ok = (image_size > 0 && image_size <= FW_UP_MAX_SIZE);

            fw_up_begin_sync_t begin_msg;
            begin_msg.image_size = image_size;
            begin_msg.image_crc  = image_crc;
            begin_msg.crc32      = 0;

            // Track the image so re-polls from the host don't redo the slave kick.
            static uint32_t s_erased_size = 0;
            static uint32_t s_erased_crc  = 0;
            bool new_image = (image_size != s_erased_size || image_crc != s_erased_crc);

            if (new_image && master_ok) {
                s_erased_size = image_size;
                s_erased_crc  = image_crc;
                // BASELINE: master is a pure relay during fw_up — no master
                // staging erase, no master core1 halt/restart.  Master flash
                // work must be re-added piecewise from here; see FW_UP_BASELINE.md.
                send_to_bridge(USER_SYNC_FW_UP_BEGIN, &begin_msg, sizeof(begin_msg), 3);
                fw_staging_set_fw_up_active(true);
                uprintf("FW_UP_BEGIN: new image size=%lu crc=0x%08lx (relay-only master)\n",
                        image_size, image_crc);
            }

            // Single slave readiness poll (no retry loop).  If slave is not ready
            // we return '~' and the host re-polls after a short delay, allowing the
            // QMK main loop to run the normal split transport between polls.
            uint8_t slave_ack = master_ok
                ? send_to_bridge(USER_SYNC_FW_UP_BEGIN, &begin_msg, sizeof(begin_msg), 1)
                : SYNC_CRC32_ERR;
            bool slave_ok = (slave_ack == SYNC_ACK);

            memset(data, 0, length);
            if (!master_ok) {
                memcpy(data, "P\x40!", 3);   // hard error: invalid image
            } else if (slave_ok) {
                memcpy(data, "P\x40.", 3);   // both halves ready — host may start chunks
            } else {
                memcpy(data, "P\x40~", 3);   // still erasing — host should re-poll
            }
            uprintf("FW_UP_BEGIN: slave_ack=0x%02x slave_ok=%d new_image=%d\n",
                    slave_ack, slave_ok, new_image);
            // Log slave state once, on the first poll where the slave reports
            // ready.  This is the snapshot just before the host starts streaming
            // chunks — if anything is wrong with slave state here it will explain
            // the very-first-chunk failure we keep seeing.
            static bool s_logged_ready_status = false;
            if (slave_ok && !s_logged_ready_status) {
                s_logged_ready_status = true;
                fw_up_log_slave_status("begin-ready");
            }
            // Also log slave state periodically while waiting for erase: every
            // ~16 polls of CRC_ERR (≈ 4 s at the host's 250 ms poll cadence) we
            // print a snapshot so we can see whether erase_sector_next is
            // actually advancing.  Without this the slave can be stuck and
            // the only visible signal is "slave_ack=0x35" forever.
            static uint16_t s_pending_poll_count = 0;
            if (!slave_ok && slave_ack == SYNC_CRC32_ERR) {
                if ((++s_pending_poll_count & 0x0F) == 0) {
                    fw_up_log_slave_status("begin-pending");
                }
            } else {
                s_pending_poll_count = 0;
            }
            if (new_image) {
                // Re-arm so a fresh fw_up after a previous failure also logs once.
                s_logged_ready_status = false;
                s_pending_poll_count = 0;
            }
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FW_UP_CHUNK: { // data[2..5]=offset, data[6..61]=56 bytes of firmware
            uint32_t offset;
            memcpy(&offset, &data[HID_DATA_IDX], 4);
            const uint8_t *chunk_data = &data[HID_DATA_IDX + 4];
            uprintf("FW_UP_CHUNK: offset=%lu\n", offset);
            // Relay to slave FIRST so master's s_next_offset only advances after slave ACKs.
            // This keeps both write cursors in sync: if the relay fails, the host can safely
            // retry the same chunk and master will accept it (offset still matches).
            fw_up_chunk_sync_t chunk_msg;
            chunk_msg.offset = offset;
            memcpy(chunk_msg.data, chunk_data, FW_UP_CHUNK_SIZE);
            chunk_msg.crc32 = 0;
            uint8_t slave_ack = send_to_bridge(USER_SYNC_FW_UP_CHUNK, &chunk_msg, sizeof(chunk_msg), 10);
            uprintf("FW_UP_CHUNK: slave_ack=0x%02x (relay-only, no master write)\n", slave_ack);
            // First-failure diagnostic: when a chunk doesn't reach the slave,
            // immediately query the slave's internal state so we can tell from
            // the master serial log whether the slave is fully hung (status RPC
            // also fails) or just rejecting the chunk (status RPC works and
            // shows the counters).  Gated to once per failure burst so we don't
            // spam if every chunk is failing.
            static bool s_logged_failure_status = false;
            if (slave_ack != SYNC_ACK && !s_logged_failure_status) {
                s_logged_failure_status = true;
                fw_up_log_slave_status("chunk-fail");
            } else if (slave_ack == SYNC_ACK) {
                s_logged_failure_status = false;  // re-arm after recovery
            }
            // BASELINE: master does NOT write to its own staging.
            // "ok" is just whether the slave accepted the relayed chunk.
            bool ok = (slave_ack == SYNC_ACK);
            memset(data, 0, length);
            memcpy(data, ok ? "P\x41." : "P\x41!", 3);
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FW_UP_COMMIT: { // verify CRC, arm commit for both sides
            // BASELINE: master does NOT finalize its own staging (no data
            // written).  Relay commit to slave, clear fw_up_active.  fw_up
            // does not yet actually update master firmware in this mode —
            // only the slave path is exercised.  See FW_UP_BASELINE.md.
            uint32_t dummy_crc = 0;
            uint8_t slave_ack = send_to_bridge(USER_SYNC_FW_UP_COMMIT, &dummy_crc, sizeof(dummy_crc), 10);
            fw_staging_set_fw_up_active(false);
            bool ok = (slave_ack == SYNC_ACK);
            memset(data, 0, length);
            memcpy(data, ok ? "P\x42." : "P\x42!", 3);
            uprintf("FW_UP_COMMIT: relay-only master slave_ack=0x%02x\n", slave_ack);
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FW_UP_GET_VERSION: { // return version string + fw_size + fw_crc
            uint32_t fw_size = fw_staging_get_own_fw_size();
            uint32_t fw_crc  = fw_staging_get_own_fw_crc();
            memset(data, 0, length);
            memcpy(data, "P\x43.", 3);
            size_t version_len = strlen(FW_VERSION);
            if (version_len >= FW_UP_VERSION_LEN) version_len = FW_UP_VERSION_LEN - 1;
            memcpy(&data[3], FW_VERSION, version_len);
            memcpy(&data[3 + FW_UP_VERSION_LEN], &fw_size, 4);
            memcpy(&data[3 + FW_UP_VERSION_LEN + 4], &fw_crc, 4);
            uprintf("FW_UP_GET_VERSION: %s size=%lu crc=0x%08lx\n", FW_VERSION, fw_size, fw_crc);
            raw_hid_send(data, length);
            return true;
        }

        default:
            return false;
    }
}
