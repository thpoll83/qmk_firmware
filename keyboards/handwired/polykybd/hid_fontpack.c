// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// HID transport for flashing the external-flash font pack. Parallels hid_fw_up.c
// but targets the resource region and, on commit, reloads fonts WITHOUT a reboot
// (the pack is data, not code). Single fixed slot, written in place.
//
// MASTER-SIDE ONLY for now: the slave half also renders keycaps and needs its
// own copy of the pack, so a follow-up bridges BEGIN/CHUNK/COMMIT to the slave
// (mirroring split_fw_up.c). Until then, flashing updates the master half's
// fonts only; the slave keeps showing resident-only fallback for pack scripts.

#include "hid_fontpack.h"

#include "raw_hid.h"
#include "config.h"
#include "base/fontpack.h"
#include "base/fw_staging.h"   // FW_UP_CHUNK_SIZE

#include <print.h>
#include <string.h>

#define HID_DATA_IDX 2

bool hid_fontpack_receive(uint8_t *data, uint8_t length) {
    switch (data[1]) {

        case CMD_FONTPACK_BEGIN: {   // data[2..5]=pack_size, data[6..9]=content_version
            uint32_t pack_size, content_version;
            memcpy(&pack_size,       &data[HID_DATA_IDX],     4);
            memcpy(&content_version, &data[HID_DATA_IDX + 4], 4);
            bool ok = fontpack_flash_begin(pack_size);
            memset(data, 0, length);
            memcpy(data, ok ? "P\x50." : "P\x50!", 3);
            raw_hid_send(data, length);
            uprintf("FONTPACK_BEGIN: size=%lu cver=%lu -> %s\n",
                    (unsigned long)pack_size, (unsigned long)content_version,
                    ok ? "ready" : "rejected");
            return true;
        }

        case CMD_FONTPACK_CHUNK: {   // data[2..5]=offset, data[6..]=FW_UP_CHUNK_SIZE bytes
            uint32_t offset;
            memcpy(&offset, &data[HID_DATA_IDX], 4);
            bool ok = fontpack_flash_write(offset, &data[HID_DATA_IDX + 4], FW_UP_CHUNK_SIZE);
            memset(data, 0, length);
            memcpy(data, ok ? "P\x51." : "P\x51!", 3);
            if (!ok) {
                // Report where to resume so the host can rewind instead of aborting.
                uint32_t resume = fontpack_flash_next_offset();
                memcpy(&data[3], &resume, 4);
                uprintf("FONTPACK_CHUNK: NACK offset=%lu resume=%lu\n",
                        (unsigned long)offset, (unsigned long)resume);
            }
            raw_hid_send(data, length);
            return true;
        }

        case CMD_FONTPACK_COMMIT: {  // verify CRC from flash + reload (no reboot)
            bool ok = fontpack_flash_finalize();
            memset(data, 0, length);
            memcpy(data, ok ? "P\x52." : "P\x52!", 3);
            if (ok) {
                uint16_t cver = fontpack_content_version();
                memcpy(&data[3], &cver, 2);
            }
            raw_hid_send(data, length);
            uprintf("FONTPACK_COMMIT: %s (present=%d fonts=%u cver=%u)\n",
                    ok ? "live" : "INVALID (resident-only)",
                    fontpack_present(), fontpack_font_count(), fontpack_content_version());
            return true;
        }

        case CMD_FONTPACK_STATUS: {  // present, abi_version, content_version, expected_abi
            memset(data, 0, length);
            memcpy(data, "P\x53.", 3);
            data[3] = fontpack_present() ? 1 : 0;
            data[4] = (uint8_t)FONTPACK_ABI_VERSION;                 // expected/compiled-in ABI
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
