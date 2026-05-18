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
            begin_msg.crc32      = 0;  // filled by send_to_bridge

            // Kick slave's deferred erase.  10 retries tolerate intermittent UART
            // blackouts from a slave mid-sector-erase (~50 ms irqs-disabled windows
            // every 70 ms).  Return value is ignored — we proceed regardless and let
            // the verification loop below confirm readiness.
            //   SYNC_ACK_SIG → slave started a new erase (new firmware)
            //   SYNC_CRC32_ERR → either UART timed out, slave still erasing from a
            //                    previous session, or old-protocol slave (all fine)
            send_to_bridge(USER_SYNC_FW_UP_BEGIN, &begin_msg, sizeof(begin_msg), 10);

            // Erase master staging synchronously (~50 ms/sector, interrupts briefly
            // re-enabled between sectors so USB stays alive).  Runs in parallel with
            // the slave's deferred erase.
            if (master_ok) {
                fw_staging_begin(image_size, image_crc);
            }

            // Verification loop: poll until slave reports ready (SYNC_ACK).
            //   SYNC_ACK     → slave erase done, ready for chunks → slave_ok = true
            //   SYNC_ACK_SIG → slave just started erasing (kick was missed and this
            //                  poll triggered a fresh start) → keep polling
            //   SYNC_CRC32_ERR → UART failure or slave still erasing → keep polling
            //
            // 60 polls × 100 ms = 6 s window.  Worst case: kick fails entirely, first
            // poll starts slave erase at ~t=3.9 s (after 10-retry kick + master erase).
            // Slave finishes at ~t=8.3 s → caught at poll ≈44.  Total ≈8.4 s < 15 s
            // host timeout.
            bool slave_ok = false;
            for (uint8_t i = 0; i < 60 && !slave_ok; i++) {
                begin_msg.crc32 = 0;
                uint8_t ack = send_to_bridge(USER_SYNC_FW_UP_BEGIN, &begin_msg, sizeof(begin_msg), 1);
                if (ack == SYNC_ACK) {
                    slave_ok = true;
                } else {
                    wait_ms(100);
                }
            }

            memset(data, 0, length);
            memcpy(data, (master_ok && slave_ok) ? "P\x40." : "P\x40!", 3);
            uprintf("FW_UP_BEGIN: size=%lu crc=0x%08lx master=%d slave_ok=%d\n",
                    image_size, image_crc, master_ok, slave_ok);
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
