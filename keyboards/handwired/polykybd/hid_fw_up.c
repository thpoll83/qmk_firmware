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

            // Track the image we've already erased so re-polls from the host
            // don't trigger redundant master erases.
            static uint32_t s_erased_size = 0;
            static uint32_t s_erased_crc  = 0;
            bool new_image = (image_size != s_erased_size || image_crc != s_erased_crc);

            if (new_image && master_ok) {
                s_erased_size = image_size;
                s_erased_crc  = image_crc;
                // Kick slave's deferred erase (3 retries), then erase master staging
                // synchronously.  We do NOT loop here waiting for the slave — that
                // would block the QMK main thread for seconds and starve the split
                // transport, causing QMK to mark the slave as disconnected.  Instead
                // we return immediately and let the host re-poll until we confirm
                // the slave is ready (reply byte '~' = "keep polling").
                send_to_bridge(USER_SYNC_FW_UP_BEGIN, &begin_msg, sizeof(begin_msg), 3);
                fw_staging_begin(image_size, image_crc);
                uprintf("FW_UP_BEGIN: new image size=%lu crc=0x%08lx, master erased\n",
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
            uprintf("FW_UP_CHUNK: slave_ack=0x%02x, writing master\n", slave_ack);
            bool ok = (slave_ack == SYNC_ACK) && fw_staging_write_chunk(offset, chunk_data, FW_UP_CHUNK_SIZE);
            uprintf("FW_UP_CHUNK: write_ok=%d sending resp\n", ok);
            memset(data, 0, length);
            memcpy(data, ok ? "P\x41." : "P\x41!", 3);
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FW_UP_COMMIT: { // verify CRC, arm commit for both sides
            // Relay commit to slave first (so slave ACKs before it reboots)
            uint32_t dummy_crc = 0;
            uint8_t slave_ack = send_to_bridge(USER_SYNC_FW_UP_COMMIT, &dummy_crc, sizeof(dummy_crc), 10);
            // Finalize master staging; apply is deferred to housekeeping
            bool ok = fw_staging_finalize();
            memset(data, 0, length);
            memcpy(data, ok ? "P\x42." : "P\x42!", 3);
            uprintf("FW_UP_COMMIT: master=%s slave_ack=0x%02x\n", ok ? "OK" : "CRC fail", slave_ack);
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
