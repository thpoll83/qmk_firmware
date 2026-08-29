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
#include <transactions.h>
#include "base/fw_staging.h"
#include "base/fontpack.h"
#include "base/update.h"        // poly_prepare_for_flash()
#include "doom/doom_mode.h"     // doom_mode_active() (inline false unless POLYKYBD_DOOM)

#include <print.h>
#include <string.h>
#include "poly_keymap.h"

#define HID_DATA_IDX 2

// Which bundle the in-progress BEGIN/CHUNK/COMMIT sequence targets — remembered
// so COMMIT can report the just-flashed bundle's content_version.
static uint8_t s_fontpack_bundle = 0;

// Writes the 3-byte reply header for a command whose status is one of several
// values: 'P', the command id, then the status byte. The sibling of hid_com.c's
// boolean `hid_reply()`, which cannot express COMMIT's three outcomes.
//
// Building the bytes instead of writing a `"P\x52R"` literal is deliberate twice
// over: the status comes from the FONTPACK_COMMIT_* constants (so the header owns
// the wire values and cannot drift from what is emitted), and there is no `\xHH`
// escape for a following letter to be swallowed into — `"P\x52C"` would be a
// single 0x52C character constant, not three bytes, and 'C' being a hex digit is
// the only thing that decides it. Nothing here can hit that trap.
static inline void fontpack_reply_status(uint8_t *data, uint8_t cmd, uint8_t status) {
    data[0] = 'P';
    data[1] = cmd;
    data[2] = status;
}

bool hid_fontpack_receive(uint8_t *data, uint8_t length) {
    switch (data[1]) {

        case CMD_FONTPACK_BEGIN: {   // data[2..5]=pack_size, data[6..9]=pack_crc, data[10]=bundle_id
            uint32_t pack_size, pack_crc;
            memcpy(&pack_size, &data[HID_DATA_IDX],     4);
            memcpy(&pack_crc,  &data[HID_DATA_IDX + 4], 4);
            uint8_t  bundle = data[HID_DATA_IDX + 8];
            s_fontpack_bundle = bundle;

            // The pseudo bundles FONTPACK_BUNDLE_DOOMWAD / _DOOMPACK select the
            // doom targets (the WHX slot / the executable engine pack slot in
            // the upper resource region — doom/PACK_DESIGN.md) — same
            // transport, different fixed slot + finalize.
            bool    is_doomwad  = (bundle == FONTPACK_BUNDLE_DOOMWAD);
            bool    is_doompack = (bundle == FONTPACK_BUNDLE_DOOMPACK);
            uint8_t target      = is_doomwad  ? FW_TARGET_DOOMWAD
                                : is_doompack ? FW_TARGET_DOOMPACK
                                              : FW_TARGET_FONTPACK;

            // Resolve the fixed slot; an unknown id (or a pack bigger than the
            // slot) is rejected. The stager writes in place at the slot. Never
            // while game mode holds core1/the pool — the stager halts core1.
            uint32_t slot_off = 0, slot_size = 0;
            bool     slot_ok;
            if (is_doomwad) {
                slot_off  = FW_DOOMWAD_SLOT_OFF;
                slot_size = FW_DOOMWAD_SLOT_SIZE;
                slot_ok   = true;
            } else if (is_doompack) {
                slot_off  = FW_DOOMPACK_SLOT_OFF;
                slot_size = FW_DOOMPACK_SLOT_SIZE;
                slot_ok   = true;
            } else {
                slot_ok = fontpack_slot(bundle, &slot_off, &slot_size);
            }
            // Doom targets: just a magic-sized floor (finalize/loader do the rest).
            uint32_t min_size  = (is_doomwad || is_doompack) ? 8u : (uint32_t)sizeof(fontpack_header_t);
            bool     master_ok = (slot_ok && !doom_mode_active() &&
                                  pack_size >= min_size && pack_size <= slot_size);
            if (slot_ok) fw_staging_set_fontpack_slot(slot_off, slot_size);

            fw_up_begin_sync_t begin_msg;
            memset(&begin_msg, 0, sizeof(begin_msg));   // deterministic padding for the CRC
            begin_msg.op         = FLASH_STAGE_BEGIN;
            begin_msg.image_size = pack_size;
            begin_msg.image_crc  = pack_crc;
            begin_msg.target     = target;
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
                // kicks the slave's deferred erase, both targeting this slot.
                fw_staging_begin_deferred_target(pack_size, pack_crc, target);
                // Fire-and-forget: kicks the slave's deferred erase. Readiness is
                // polled by the slave_ack send_to_bridge below (and on re-polls).
                send_to_bridge(USER_SYNC_FLASH_STAGE, &begin_msg, sizeof(begin_msg), 3);
                uprintf("FONTPACK_BEGIN: bundle=%u size=%lu crc=0x%08lx (master+slave staging)\n",
                        bundle, (unsigned long)pack_size, (unsigned long)pack_crc);
            }

            uint8_t slave_ack = master_ok
                ? send_to_bridge(USER_SYNC_FLASH_STAGE, &begin_msg, sizeof(begin_msg), 1)
                : SYNC_GIVEUP;   // never asked — the master rejected the pack itself
            bool slave_ok    = (slave_ack == SYNC_ACK);
            bool master_done = !fw_staging_erase_pending();

            // Telemetry for the FW-9 3rd-doom-flash hang: when OUR erase is done but
            // the slave still isn't ready, surface WHAT the slave answered. The rig
            // reads the master console only, so this is the one place that can tell
            // "slave alive, still erasing" (SYNC_ACK_SIG / SYNC_BUSY) apart from
            // "slave not answering / wedged" (SYNC_GIVEUP / SYNC_CRC32_ERR). Change-
            // triggered so it never floods the ~1 Hz re-poll.
            if (master_ok && master_done && !slave_ok) {
                static uint8_t s_last_begin_slave_ack = 0xFF;
                if (slave_ack != s_last_begin_slave_ack) {
                    s_last_begin_slave_ack = slave_ack;
                    uprintf("FONTPACK_BEGIN: master erased, slave not ready (bundle=%u slave_ack=0x%02x)\n",
                            bundle, slave_ack);
                }
            }

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
            // "FONTPACK_CHUNK" tag = the same per-retry / retry-success debug
            // logging the standalone fontpack loop had (added for link diagnostics),
            // now produced by the shared helper.
            bool ok = fw_up_relay_chunk_to_slave(offset, chunk_data, "FONTPACK_CHUNK");
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
            fw_up_commit_sync_t commit_msg = { .crc32 = 0, .op = FLASH_STAGE_COMMIT };
            uint8_t slave_ack = send_to_bridge(USER_SYNC_FLASH_STAGE, &commit_msg, sizeof(commit_msg), 10);
            bool master_ok = fw_staging_finalize();   // FONTPACK target: verifies CRC + fontpack_reload()
            bool is_doom = s_fontpack_bundle == FONTPACK_BUNDLE_DOOMWAD ||
                           s_fontpack_bundle == FONTPACK_BUNDLE_DOOMPACK;
            bool slave_ok = (slave_ack == SYNC_ACK);
            bool ok = slave_ok && master_ok;

            // THREE distinct statuses, not one '!'. A dropped split-link ACK and a
            // staged-CRC mismatch are opposite events with opposite remedies, and
            // collapsing them made the host report "CRC mismatch or the font pack was
            // rejected" for a pack whose CRC was perfect and whose data was already
            // live on the master (field 2026-08-17) — which sent the diagnosis the
            // wrong way for two rounds. Exactly the confusion FW_UP_COMMIT was split
            // into four statuses to end; this is the same mistake one command over.
            //   '.' both halves finalized (the slave's font-table reload is deferred to
            //       its housekeeping by design, and cannot fail — see hid_fontpack.h)
            //   'R' the MASTER's finalize REJECTED the image (staged CRC mismatch, or
            //       the flashed slot did not load as a valid PlyF) — a real data
            //       problem; the host must re-flash.
            //   'L' the master committed but the slave did not ANSWER within the
            //       bridge's retries (a LINK failure). The master's copy is live and
            //       correct; re-sending COMMIT is free (finalize leaves
            //       s_staged_crc/s_image_crc and the write cursor untouched, and the
            //       slave's flash_stage_commit is likewise idempotent), so the host
            //       retries rather than re-streams. A slave that ANSWERED and refused
            //       is NOT this case — it is 'R', because retrying cannot change what
            //       is in that half's flash.
            // Only ask the slave when its answer is genuinely ambiguous: a refusal ack
            // is self-describing, and if the MASTER already rejected the image the
            // status is 'R' regardless — so short-circuit rather than spend an RPC
            // inside raw_hid_receive(), which runs on the main loop.
            bool slave_refused = !slave_ok && master_ok &&
                                 fw_up_slave_refused_commit(slave_ack, "FONTPACK_COMMIT");
            uint8_t status = fontpack_commit_status(master_ok, slave_ok, slave_refused);
            memset(data, 0, length);
            fontpack_reply_status(data, CMD_FONTPACK_COMMIT, status);
            // Report the slot's content_version whenever the MASTER's copy is live —
            // including 'L', where it is precisely what tells the host the data landed
            // and only the acknowledgement was lost.
            if (master_ok && !is_doom) {
                uint16_t cver = fontpack_bundle_version(s_fontpack_bundle);
                memcpy(&data[3], &cver, 2);
            }
            const char *outcome = ok ? (is_doom ? "installed" : "live")
                                     : !master_ok    ? "REJECTED (master finalize)"
                                     : slave_refused ? "REJECTED (slave refused)"
                                                     : "UNCONFIRMED (slave ACK lost)";
            if (is_doom) {
                uprintf("FONTPACK_COMMIT: %s slave=0x%02x master=%d -> %s\n",
                        s_fontpack_bundle == FONTPACK_BUNDLE_DOOMWAD ? "DOOMWAD" : "DOOMPACK",
                        slave_ack, master_ok, outcome);
            } else {
                uprintf("FONTPACK_COMMIT: bundle=%u slave=0x%02x master=%d -> %s (present=%d fonts=%u cver=%u)\n",
                        s_fontpack_bundle, slave_ack, master_ok, outcome,
                        fontpack_bundle_present(s_fontpack_bundle), fontpack_font_count(),
                        fontpack_bundle_version(s_fontpack_bundle));
            }
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
