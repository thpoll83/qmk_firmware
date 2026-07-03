// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// HID transport for flashing the external-flash "PlyF" font pack to BOTH halves.
// It reuses the firmware-update machinery: fw_staging with FW_TARGET_FONTPACK
// (deferred erase, page accumulation, dual-core flash safety, the slave bridge
// and relay-first chunk protocol) — only the flash region and the finalize
// action differ (write in place at the resource region; on commit re-load fonts
// with NO reboot). The slave routes by the `target` byte carried in the BEGIN
// transaction and remembers it for the chunk/commit transactions.

#include "hid_fontpack.h"

#include QMK_KEYBOARD_H
#include "raw_hid.h"
#include "config.h"
#include "split_fw_up.h"
#include "bridge_helper.h"
#include "polymod_crc32.h"
#include <transactions.h>
#include "base/fw_staging.h"
#include "base/fontpack.h"
#include "base/update.h"   // poly_prepare_for_flash()

#include <print.h>
#include <string.h>

#define HID_DATA_IDX 2

// Which bundle the in-progress BEGIN/CHUNK/COMMIT sequence targets — remembered
// so COMMIT can report the just-flashed bundle's content_version.
static uint8_t s_fontpack_bundle = 0;

bool hid_fontpack_receive(uint8_t *data, uint8_t length) {
    switch (data[1]) {

        case CMD_FONTPACK_BEGIN: {   // data[2..5]=pack_size, data[6..9]=pack_crc, data[10]=bundle_id
            uint32_t pack_size, pack_crc;
            memcpy(&pack_size, &data[HID_DATA_IDX],     4);
            memcpy(&pack_crc,  &data[HID_DATA_IDX + 4], 4);
            uint8_t  bundle = data[HID_DATA_IDX + 8];
            s_fontpack_bundle = bundle;

            // Resolve the bundle's fixed slot; an unknown id (or a pack bigger than
            // the slot) is rejected. The stager writes in place at the slot.
            uint32_t slot_off = 0, slot_size = 0;
            bool     slot_ok  = fontpack_slot(bundle, &slot_off, &slot_size);
            bool master_ok = (slot_ok && pack_size >= sizeof(fontpack_header_t) &&
                              pack_size <= slot_size);
            if (slot_ok) fw_staging_set_fontpack_slot(slot_off, slot_size);

            fw_up_begin_sync_t begin_msg;
            memset(&begin_msg, 0, sizeof(begin_msg));   // deterministic padding for the CRC
            begin_msg.image_size = pack_size;
            begin_msg.image_crc  = pack_crc;
            begin_msg.target     = FW_TARGET_FONTPACK;
            begin_msg.bundle     = bundle;
            begin_msg.crc32      = 0;

            // Dedup re-polls so a new image kicks the erase once (mirrors hid_fw_up).
            // Keyed on bundle too, so switching bundles re-triggers the slot erase.
            static uint32_t s_erased_size   = 0;
            static uint32_t s_erased_crc    = 0;
            static uint8_t  s_erased_bundle = 0xFF;
            bool new_image = (pack_size != s_erased_size || pack_crc != s_erased_crc ||
                              bundle != s_erased_bundle);

            if (new_image && master_ok) {
                s_erased_size   = pack_size;
                s_erased_crc    = pack_crc;
                s_erased_bundle = bundle;
                // Drop to the base layer + refresh before the flash holds the main
                // loop, so the user can still type plain characters meanwhile.
                poly_prepare_for_flash();
                // Master stages its OWN copy (deferred erase via housekeeping) and
                // kicks the slave's deferred erase, both targeting this bundle slot.
                fw_staging_begin_deferred_target(pack_size, pack_crc, FW_TARGET_FONTPACK);
                // Fire-and-forget: kicks the slave's deferred erase. Readiness is
                // polled by the slave_ack send_to_bridge below (and on re-polls).
                send_to_bridge(USER_SYNC_FW_UP_BEGIN, &begin_msg, sizeof(begin_msg), 3);
                uprintf("FONTPACK_BEGIN: bundle=%u size=%lu crc=0x%08lx (master+slave staging)\n",
                        bundle, (unsigned long)pack_size, (unsigned long)pack_crc);
            }

            uint8_t slave_ack = master_ok
                ? send_to_bridge(USER_SYNC_FW_UP_BEGIN, &begin_msg, sizeof(begin_msg), 1)
                : SYNC_CRC32_ERR;
            bool slave_ok    = (slave_ack == SYNC_ACK);
            bool master_done = !fw_staging_erase_pending();

            memset(data, 0, length);
            if (!master_ok) {
                memcpy(data, "P\x50!", 3);   // invalid size
            } else if (slave_ok && master_done) {
                memcpy(data, "P\x50.", 3);   // both halves erased — host may stream chunks
            } else {
                memcpy(data, "P\x50~", 3);   // still erasing — host re-polls
            }
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FONTPACK_CHUNK: {   // data[2..5]=offset, data[6..]=FW_UP_CHUNK_SIZE bytes
            uint32_t offset;
            memcpy(&offset, &data[HID_DATA_IDX], 4);
            const uint8_t *chunk_data = &data[HID_DATA_IDX + 4];

            // Relay to the slave FIRST (identity-bound reply: a chunk only counts
            // once the slave's write cursor moved PAST this offset), then write the
            // master's own copy — so a failed relay leaves both cursors in lock-step.
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
                    if (debug_enable && retry > 0) {
                        uprintf("FONTPACK_CHUNK relay ok on retry %u (offset=%lu slave_next=%lu)\n",
                                retry, (unsigned long)offset, (unsigned long)reply.next_offset);
                    }
                    break;
                }
                if (debug_enable) {
                    uprintf("FONTPACK_CHUNK relay retry %u (offset=%lu success=%d ack=0x%02x slave_next=%lu)\n",
                            retry, (unsigned long)offset, (int)ok_rpc, reply.ack, (unsigned long)reply.next_offset);
                }
            }
            bool ok = (slave_ack == SYNC_ACK);
            if (ok) ok = fw_staging_write_chunk(offset, chunk_data, FW_UP_CHUNK_SIZE);

            memset(data, 0, length);
            memcpy(data, ok ? "P\x51." : "P\x51!", 3);
            if (!ok) {
                // Resume point = lower of the two halves' cursors (both ACK dups).
                uint32_t resume = fw_staging_next_offset();
                memcpy(&data[3], &resume, 4);
                uprintf("FONTPACK_CHUNK: NACK offset=%lu resume=%lu\n",
                        (unsigned long)offset, (unsigned long)resume);
            }
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FONTPACK_COMMIT: {   // slave finalize+reload, then master finalize+reload (no reboot)
            uint32_t dummy = 0;
            uint8_t slave_ack = send_to_bridge(USER_SYNC_FW_UP_COMMIT, &dummy, sizeof(dummy), 10);
            bool master_ok = fw_staging_finalize();   // FONTPACK target: verifies CRC + fontpack_reload()
            bool ok = (slave_ack == SYNC_ACK) && master_ok;
            memset(data, 0, length);
            memcpy(data, ok ? "P\x52." : "P\x52!", 3);
            if (ok) {
                uint16_t cver = fontpack_bundle_version(s_fontpack_bundle);
                memcpy(&data[3], &cver, 2);
            }
            uprintf("FONTPACK_COMMIT: bundle=%u slave=0x%02x master=%d -> %s (present=%d fonts=%u cver=%u)\n",
                    s_fontpack_bundle, slave_ack, master_ok, ok ? "live" : "INVALID",
                    fontpack_bundle_present(s_fontpack_bundle), fontpack_font_count(),
                    fontpack_bundle_version(s_fontpack_bundle));
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FONTPACK_STATUS: {   // present, expected_abi, content_version, font_count
            memset(data, 0, length);
            memcpy(data, "P\x53.", 3);
            data[3] = fontpack_present() ? 1 : 0;
            data[4] = (uint8_t)FONTPACK_ABI_VERSION;
            uint16_t cver = fontpack_content_version();
            memcpy(&data[5], &cver, 2);
            data[7] = fontpack_font_count();
            raw_hid_send(data, length);
            uprintf("FONTPACK_STATUS: present=%d abi=%u cver=%u fonts=%u\n",
                    fontpack_present(), (unsigned)FONTPACK_ABI_VERSION,
                    fontpack_content_version(), fontpack_font_count());
            return true;
        }

        default:
            return false;
    }
}
