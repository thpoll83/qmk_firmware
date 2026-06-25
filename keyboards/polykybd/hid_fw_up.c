// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hid_fw_up.h"

#include QMK_KEYBOARD_H
#include "raw_hid.h"
#include "config.h"
#include "split_fw_up.h"
#include "bridge_helper.h"
#include "polymod_crc32.h"
#include <transactions.h>
#include "base/fw_staging.h"
#include "base/update.h"   // poly_prepare_for_flash()
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
    fw_up_status_request_t req = { .crc32 = 0, .op = FLASH_STAGE_STATUS, .dummy = 0 };
    fw_up_status_reply_t   reply;
    memset(&reply, 0, sizeof(reply));
    // Use transaction_rpc_exec directly — send_to_bridge hardcodes the
    // 1-byte poly_sync_reply_t and can't carry our bigger status struct.
    bool ok = transaction_rpc_exec(USER_SYNC_FLASH_STAGE,
                                   sizeof(req), &req,
                                   sizeof(reply), &reply);
    if (!ok) {
        uprintf("slave status (%s): RPC FAILED — slave unresponsive\n", tag);
        return;
    }
    const fw_staging_status_t *s = &reply.status;
    uprintf("slave status (%s): init=%u active=%u erase_pending=%u erase=%u/%u "
            "next_off=%lu begin_calls=%u chunk_calls=%u chunk_errs=%u "
            "last_chunk_off=%lu last_ack=0x%02x pd_calls=%u pd_advances=%u\n",
            tag,
            (unsigned)s->initialized, (unsigned)s->fw_up_active, (unsigned)s->erase_pending,
            (unsigned)s->erase_sector_next, (unsigned)s->erase_sector_count,
            (unsigned long)s->next_offset,
            (unsigned)s->begin_handler_calls, (unsigned)s->chunk_handler_calls,
            (unsigned)s->chunk_handler_errors,
            (unsigned long)s->last_chunk_offset, (unsigned)s->last_chunk_ack,
            (unsigned)s->process_deferred_calls,
            (unsigned)s->process_deferred_advances);
}

bool hid_fw_up_receive(uint8_t *data, uint8_t length) {
    switch (data[1]) {

        case CMD_FW_UP_BEGIN: { // data[2..5]=image_size, data[6..9]=image_crc
            uint32_t image_size, image_crc;
            memcpy(&image_size, &data[HID_DATA_IDX],   4);
            memcpy(&image_crc,  &data[HID_DATA_IDX+4], 4);
            bool master_ok = (image_size > 0 && image_size <= FW_UP_MAX_SIZE);

            fw_up_begin_sync_t begin_msg;
            memset(&begin_msg, 0, sizeof(begin_msg));   // deterministic padding for the CRC
            begin_msg.op         = FLASH_STAGE_BEGIN;
            begin_msg.image_size = image_size;
            begin_msg.image_crc  = image_crc;
            begin_msg.target     = FW_TARGET_FIRMWARE;
            begin_msg.crc32      = 0;

            // Track the image so re-polls from the host don't redo the slave kick.
            static uint32_t s_erased_size = 0;
            static uint32_t s_erased_crc  = 0;
            bool new_image = (image_size != s_erased_size || image_crc != s_erased_crc);

            if (new_image && master_ok) {
                s_erased_size = image_size;
                s_erased_crc  = image_crc;
                // Drop to the base layer + refresh before the flash holds the main
                // loop, so the user can still type plain characters meanwhile.
                poly_prepare_for_flash();
                // PHASE 1 (2026-05-30): the master now stages its OWN copy as well.
                // Use the deferred erase — the same proven path as the slave — so
                // the master's USB stays alive: housekeeping_task_user() drives the
                // master's fw_staging_process_deferred() one sector per 70 ms.
                // begin_deferred also halts the master's core1 and sets fw_up_active.
                fw_staging_begin_deferred(image_size, image_crc);
                // Kick the slave's deferred erase so both halves erase in parallel.
                send_to_bridge(USER_SYNC_FLASH_STAGE, &begin_msg, sizeof(begin_msg), 3);
                uprintf("FW_UP_BEGIN: new image size=%lu crc=0x%08lx (master+slave staging)\n",
                        image_size, image_crc);
            }

            // Single slave readiness poll (no retry loop).  If either half is not
            // ready we return '~' and the host re-polls after a short delay, letting
            // the QMK main loop advance both deferred erases between polls.
            uint8_t slave_ack = master_ok
                ? send_to_bridge(USER_SYNC_FLASH_STAGE, &begin_msg, sizeof(begin_msg), 1)
                : SYNC_CRC32_ERR;
            bool slave_ok    = (slave_ack == SYNC_ACK);
            bool master_done = !fw_staging_erase_pending();   // master's own staging erased?

            memset(data, 0, length);
            if (!master_ok) {
                memcpy(data, "P\x40!", 3);   // hard error: invalid image
            } else if (slave_ok && master_done) {
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
            if (slave_ok && master_done && !s_logged_ready_status) {
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
#ifdef FW_UP_VERBOSE
            uprintf("FW_UP_CHUNK: offset=%lu\n", offset);
            // DIAGNOSTIC (2026-05-29, run 3): bracket the very first chunk to
            // localise the slave hang.  The core1-restart probe (run 3) did NOT
            // help, and normal overlay sync pushes 67-69 B M2S to the slave fine
            // — larger than this 64 B chunk — so neither core1 state nor payload
            // size is the cause.  Do a small status read (transport known good at
            // begin-ready) then a 64 B-M2S status read (the status handler ignores
            // in_len, so a padded request is benign and read-only):
            //   small OK, large OK   -> transport fine -> the FW_UP_CHUNK txn itself is the culprit
            //   small OK, large FAIL -> large-M2S transport broken in post-erase fw_up state
            //   small FAIL           -> slave already hung post-erase, before any chunk
            // See FW_UP_BASELINE.md (run 3).
            if (offset == 0) {
                fw_up_log_slave_status("pre-chunk");
                uint8_t              big_req[64];
                fw_up_status_reply_t big_reply;
                memset(big_req, 0xA5, sizeof(big_req));
                uint32_t st_op = FLASH_STAGE_STATUS;   // op selector routes to the STATUS handler
                memcpy(&big_req[4], &st_op, sizeof(st_op));
                memset(&big_reply, 0, sizeof(big_reply));
                bool big_ok = transaction_rpc_exec(USER_SYNC_FLASH_STAGE,
                                                   sizeof(big_req), big_req,
                                                   sizeof(big_reply), &big_reply);
                uprintf("FW_UP probe: pre-chunk 64B-M2S status xfer -> %s\n",
                        big_ok ? "OK" : "FAILED");
            }
#endif
            // Relay to slave FIRST so master's s_next_offset only advances after slave ACKs.
            // This keeps both write cursors in sync: if the relay fails, the host can safely
            // retry the same chunk and master will accept it (offset still matches).
            //
            // The reply is the identity-bound fw_up_chunk_reply_t, NOT the bare 1-byte
            // poly_sync_reply_t: a chunk only counts as delivered when the slave's
            // post-RPC write cursor moved PAST this chunk's offset.  With the bare ACK,
            // a stale reply left over from the previous chunk was indistinguishable
            // from the real one and silently desynchronised the cursors — the slave
            // ended up one chunk behind and rejected the whole rest of the stream
            // (2026-06-10, updates dying at 6% / 83%).  A duplicate re-send of an
            // already-staged chunk has next_offset > offset and passes (idempotent).
            uint8_t slave_ack = fw_up_relay_chunk_to_slave(offset, chunk_data, "FW_UP_CHUNK") ? SYNC_ACK : SYNC_CRC32_ERR;
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
            // PHASE 1: the master writes the same chunk to its OWN staging, but
            // only after the slave accepted it.  Relaying first keeps both write
            // cursors in lock-step — a failed relay leaves the master's s_next_offset
            // untouched, so the host can safely retry the same offset on both halves.
            bool ok = (slave_ack == SYNC_ACK);
            if (ok) {
                ok = fw_staging_write_chunk(offset, chunk_data, FW_UP_CHUNK_SIZE);
                if (!ok) uprintf("FW_UP_CHUNK: master staging write FAILED offset=%lu\n", offset);
            }
#ifdef FW_UP_VERBOSE
            uprintf("FW_UP_CHUNK: slave_ack=0x%02x master_write_ok=%d\n", slave_ack, ok);
#endif
            memset(data, 0, length);
            memcpy(data, ok ? "P\x41." : "P\x41!", 3);
            if (!ok) {
                // Tell the host where the stream can be resumed: the lower of the
                // two halves' write cursors.  Both halves ACK duplicate chunks
                // idempotently, so the host can rewind to this offset and re-send
                // from there instead of aborting the whole update.  If the slave
                // is unreachable its cursor is unknown — report the master's own
                // (the host will retry the current chunk with backoff).
                uint32_t resume = fw_staging_next_offset();
                fw_up_status_request_t sreq = { .crc32 = 0, .op = FLASH_STAGE_STATUS, .dummy = 0 };
                fw_up_status_reply_t   srep;
                memset(&srep, 0, sizeof(srep));
                if (transaction_rpc_exec(USER_SYNC_FLASH_STAGE, sizeof(sreq), &sreq, sizeof(srep), &srep) &&
                    srep.crc32 == crc32_1byte((const uint8_t *)&srep.status, sizeof(srep.status), 0) &&
                    srep.status.next_offset < resume) {
                    resume = srep.status.next_offset;
                }
                memcpy(&data[3], &resume, 4);
                uprintf("FW_UP_CHUNK: NACK offset=%lu resume=%lu\n", offset, resume);
            }
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FW_UP_SIGNATURE: { // FW-2: data[2]=part(0/1), data[3..34]=32 sig bytes
            // The 64-byte Ed25519 image signature arrives in two 32-byte parts
            // (it doesn't fit one 64-byte HID report). Reassemble, then hand it to
            // the stager; the master verifies it at COMMIT (fw_staging_finalize).
            static uint8_t s_sig_buf[FW_SIG_LEN];
            uint8_t part = data[HID_DATA_IDX];
            bool ok = (part < 2);
            if (ok) {
                memcpy(&s_sig_buf[(uint16_t)part * 32], &data[HID_DATA_IDX + 1], 32);
                if (part == 1) {
                    fw_staging_set_signature(s_sig_buf);
                }
            }
            memset(data, 0, length);
            memcpy(data, ok ? "P\x45." : "P\x45!", 3);
            raw_hid_send(data, length);
            uprintf("FW_UP_SIGNATURE: part=%u %s\n", part, ok ? "stored" : "BAD");
            return true;
        }

        case CMD_FW_UP_COMMIT: { // verify staged CRC on both halves (no apply/reboot yet)
            // Relay COMMIT to the slave first (it finalizes + verifies its own
            // staged copy), then finalize the master's own staged copy.  Decoupled
            // milestone: fw_staging_finalize() verifies the O(1) running CRC, clears
            // fw_up_active and restarts core1 — it does NOT apply or reboot.  Actually
            // installing the staged image is the explicit FW_UP_APPLY step (phase 2).
            fw_up_commit_sync_t commit_msg = { .crc32 = 0, .op = FLASH_STAGE_COMMIT };
            uint8_t slave_ack  = send_to_bridge(USER_SYNC_FLASH_STAGE, &commit_msg, sizeof(commit_msg), 10);
            bool master_ok = fw_staging_finalize();   // also clears fw_up_active + restarts master core1
            bool ok = (slave_ack == SYNC_ACK) && master_ok;
            memset(data, 0, length);
            memcpy(data, ok ? "P\x42." : "P\x42!", 3);
            uprintf("FW_UP_COMMIT: slave_ack=0x%02x master_finalize=%d\n", slave_ack, master_ok);
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FW_UP_APPLY: { // PHASE 2: install the staged image on the MASTER only
            // Hardware-verified self-flash (always built in): do_apply copies the
            // staged image to flash offset 0 while keeping the live vector table
            // intact until last — app body first with IRQs toggled per sector, then
            // sector 0 with VTOR relocated to RAM — so the brick the naive
            // erase-from-zero caused (FW_UP_BASELINE.md "Phase 2 — run 2") can't
            // recur.  Failure is still master-only and BOOTSEL/UF2-recoverable.
            bool ok = fw_staging_has_valid_staged_image();
            memset(data, 0, length);
            memcpy(data, ok ? "P\x44." : "P\x44!", 3);
            raw_hid_send(data, length);
            uprintf("FW_UP_APPLY: master self-apply %s\n",
                    ok ? "armed (rebooting…)" : "REJECTED (no valid staged image)");
            if (ok) {
                // Tell the slave to install its staged image and reboot too, so
                // BOTH halves restart together (like a replug).  A master-only
                // reboot leaves the slave running stale → the rebooted master
                // can't re-sync to it and hangs on the boot splash.  This also
                // finally updates the slave's firmware: it staged + committed the
                // image but, before this, was never told to apply it.  (The slave
                // obeys this only once it already runs firmware that has the apply
                // handler — see FW_UP_BASELINE.md for the OLD→NEW bootstrap note.)
                poly_reset_sync_t apply_msg = { .crc32 = 0, .magic = POLY_RESET_MAGIC,
                                                .action = RESET_ACTION_APPLY };
                // Hardened handoff (field 2026-06-22): an under-retried bridge here
                // let the slave miss the apply once — the master then rebooted alone
                // and hung on the boot splash waiting for a slave that never
                // restarted (manual replug required). Use 20 retries and re-fire the
                // whole round once if the slave still hasn't acked. The slave apply
                // is idempotent (it only validates the staged image + arms a deferred
                // reboot), send_to_bridge is synchronous (returns only after the slave
                // has handled it, so it's safe to reboot the master once we see the
                // ack), and we're about to reboot anyway — the extra worst-case ~1 s
                // is free insurance against a one-shot drop on this critical step.
                uint8_t slave_ack = send_to_bridge(USER_SYNC_RESET, &apply_msg, sizeof(apply_msg), 20);
                if (slave_ack != SYNC_ACK) {
                    slave_ack = send_to_bridge(USER_SYNC_RESET, &apply_msg, sizeof(apply_msg), 20);
                }
                uprintf("FW_UP_APPLY: slave apply+reboot (USER_SYNC_RESET) ack=0x%02x\n", slave_ack);
                fw_staging_arm_apply();   // housekeeping → fw_staging_apply_and_reboot()
            }
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
