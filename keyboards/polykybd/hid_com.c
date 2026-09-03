// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_com.h"
#include "hid_fw_up.h"
#include "poly_macro.h"
#include "hid_fontpack.h"
#include "split_fw_up.h"

#include QMK_KEYBOARD_H
#include "quantum.h"

#include "raw_hid.h"

#include "state.h"
#include "anim/startup_anim.h"
#include "side.h"
#include "profiling/loop_profile.h"
#include "config.h"
#include "split_sync.h"
#include "bridge_helper.h"
#include "matrix_helper.h"
#include "fill_overlay.h"
#include "lang/lang_lut.h"
#include "base/com.h"
#include "base/overlay.h"
#include "doom/doom_mode.h"   // Doom easter egg (inline no-ops unless POLYKYBD_DOOM)
#include "base/fontpack.h"
#include "base/update.h"
#include "poly_util.h"

#include <print.h>
#include <transactions.h>
#include <dynamic_keymap.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "poly_keymap.h"
#include "layer_names.h"
#include "base/crash_record.h"


/*[[[cog
import cog
import os
from textwrap import wrap
from openpyxl import load_workbook
# Shared cog constant for all language-list report generators below (mirrors the
# C RAW_EPSIZE macro = raw HID report size). Defined here, in the first cog block,
# so the per-command blocks chunk payloads against one value instead of each
# redefining it.
RAW_EPSIZE = 64
wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang", "lang_lut.xlsx"))
sheet = wb['key_lut']

languages = []
lang_index = 0
lang_key = sheet["B1"].value
while lang_key:
    lang_key = lang_key.replace("-", "")
    languages.append(lang_key)
    lang_index = lang_index + 1
    lang_key = sheet.cell(row = 1, column = 2 + lang_index*4).value
]]]*/
//[[[end]]]

void invert_display(uint8_t r, uint8_t c, bool state);
bool key_has_display(uint8_t r, uint8_t c);
// Defined in the shared keyboard-level poly_keymap.c; declared here so the
// shared HID dispatcher can drive the display-off command (case 24).

// Set on boot; cleared after the first GET_ID exchange so the host can detect
// a firmware restart even when it never lost the USB connection.
static bool s_fresh_boot = true;

void poly_mark_fresh_boot(void) {
    s_fresh_boot = true;
}


// Notifies RGB/LED matrix of key event for animation effects based on key press state.
void switch_events_poly(uint8_t row, uint8_t col, bool pressed) {
#if defined(LED_MATRIX_ENABLE)
    led_matrix_handle_key_event(row, col, pressed);
#endif
#if defined(RGB_MATRIX_ENABLE)
    rgb_matrix_handle_key_event(row, col, pressed);
#endif
}

// Writes the 3-byte HID response header into `data`: 'P', the command id, then
// '.' for ACK or '!' for NACK — replacing the repeated `memcpy(data, "P\xNN.", 3)`
// literals (the command byte reads as a real number here, not a hex escape).
// Callers still own the surrounding memset and the raw_hid_send.
static inline void hid_reply(uint8_t *data, uint8_t cmd, bool ok) {
    data[0] = 'P';
    data[1] = cmd;
    data[2] = ok ? '.' : '!';
}

// Bytes of report payload actually present at &data[header] (header = the count of
// bytes before the payload, e.g. report-id + command + sub-fields). Centralises the
// "clamp a host length/size to what the fixed-size report holds" arithmetic so the
// bounds checks below can't drift into off-by-ones (SECURITY: FW-3 / FW-5 / FW-7).
static inline uint16_t hid_payload_avail(uint8_t length, uint8_t header) {
    return length > header ? (uint16_t)(length - header) : 0;
}

bool legacy_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);
    uint8_t data_len = 0;

    switch(*command_id) {
        case id_dynamic_keymap_reset:
            dynamic_keymap_reset_poly();
            data_len = 1;
            break;
        case id_dynamic_keymap_set_keycode:
            dynamic_keymap_set_keycode_poly(command_data[0], command_data[1], command_data[2], (command_data[3] << 8) | command_data[4]);
            data_len = 6;
            break;
        case id_dynamic_keymap_set_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
            // SECURITY (FW-5): `size` is a host byte (0..255) but the payload lives in
            // the fixed-size report at &command_data[3]. Reading `size` bytes past it
            // over-reads the report buffer (and, once this report is bridged verbatim,
            // the slave's copy). Clamp to the bytes actually present and write the
            // clamped value back so the bridged report carries the safe size too.
            uint16_t avail = hid_payload_avail(length, 4);
            if (size > avail) { size = avail; command_data[2] = (uint8_t)size; }
            uprintf("Set dynamic buffer offset: %u, size: %u\n", offset, size);
            dynamic_keymap_set_buffer_poly(offset, size, &command_data[3]);
            data_len = RAW_EPSIZE;
            break;
        }
        case id_dynamic_keymap_get_layer_count:
            command_data[0] = DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT;//dynamic_keymap_get_layer_count();
            uprintf("Get dynamic layer count: %u.\n", command_data[0]);
            raw_hid_send(data, length);
            return true;
        case id_dynamic_keymap_get_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
            // SECURITY (FW-3): clamp the host `size` to the report space at
            // &command_data[3]. dynamic_keymap_get_buffer writes `size` bytes there with
            // no destination bound, so an unclamped 0..255 overruns the report buffer.
            uint16_t avail = hid_payload_avail(length, 4);
            if (size > avail) size = avail;
            uprintf("Get dynamic buffer offset: %u, size: %u\n", offset, size);
            dynamic_keymap_get_buffer(offset, size, &command_data[3]);
            raw_hid_send(data, length);
            return true;
        }
        default:
            return false;
    }
    dynamic_keymap_sync_t sync_data;
    memcpy(&sync_data.commands, data, data_len);
    send_to_bridge(USER_SYNC_DYNAMIC_KEYMAP_DATA, (void*)&sync_data, sizeof(sync_data.crc32)+data_len, 10);
    request_disp_refresh();
    raw_hid_send(data, length);
    return true;
}

// Handles HID commands: device ID, language change, overlay reception, mapping, and display control.
// Global variables: hid_keycode, hid_modifier, hid_roi, hid_bit_index, hid_bit_index_bridge
void raw_hid_receive(uint8_t *data, uint8_t length) {
    // Board name in the GET_ID reply — each variant header (QMK_KEYBOARD_H) may
    // define POLY_KB_NAME; default to "Split72" so split72 (which doesn't define
    // it) emits the exact same string as before.
#ifndef POLY_KB_NAME
#    define POLY_KB_NAME "Split72"
#endif
    const char * name = "P\x06." POLY_KB_NAME " " FW_VERSION " P" STR(PROTOCOL_VERSION) " HW" STR(DEVICE_VER) " ";

    if (length<1) {
        return;
    }

    if(data[0] == id_custom_save || data[0] == 'P') {
        // Doom easter egg: while game mode has borrowed the overlay pool as
        // game memory, drop the pool-writing bulk commands (all ACKless — a
        // reply here would inject a stale report into the host's read stream).
        // Every other command keeps answering, GET_ID included.
        if (doom_hid_frozen(data[1])) {
            return;
        }
        // Crash-record breadcrumb: a watchdog timeout inside a handler names the
        // command. Housekeeping resets the tag to LOOP on its next pass.
        (void)crash_phase_enter(CRASH_PHASE_HID, data[1]);
        const poly_layer_t* local_layer = get_local_layer();
        poly_sync_t* local_state = access_local_state();
        // Bulk overlay/mapping commands: plain (10), flags on/off (11/12), compressed
        // (16/17), ROI (18/19), mapping (21). Two markers:
        //  - note_overlay_activity() timestamps the burst so sync_and_refresh_displays()
        //    can coalesce the many per-report renders of a program switch into one.
        //  - loop_profile_note_overlay_cmd() tags the iteration for the timing profiler
        //    (no-op unless POLYKYBD_LOOP_PROFILE).
        switch (data[1]) {
            case 10: case 11: case 12: case 16: case 17: case 18: case 19: case 21: case 33:
                note_overlay_activity();
                loop_profile_note_overlay_cmd();
                break;
            default:
                break;
        }
        switch(data[1]) {
            // case id_custom_channel...id_qmk_led_matrix_channel: //maybe now usable :)
            //     break;
            case 6: { //id
                memset(data, 0, length);
                size_t nlen = strlen(name);
                memcpy(data, name, nlen);
                if (s_fresh_boot) {
                    data[2] = '*'; // host sees '*' instead of '.' -> firmware just booted
                    s_fresh_boot = false;
                }
                // Per-bundle font-pack content versions, placed AFTER the string's
                // NUL terminator so the FW/protocol/HW string stays free to grow.
                // Block = ['V'][count][u16 little-endian version × count] in bundle
                // order (PROTOCOL_VERSION >= 6). Older hosts stop at the NUL and
                // ignore it; the host flashes only bundles whose device version is
                // behind its shipped version.
                uint8_t bcount = fontpack_bundle_count();
                size_t  off    = nlen + 1;   // skip the NUL
                if (off + 2u + (size_t)bcount * 2u <= length) {
                    data[off++] = 'V';
                    data[off++] = bcount;
                    for (uint8_t b = 0; b < bcount; ++b) {
                        uint16_t v = fontpack_bundle_version(b);
                        data[off++] = (uint8_t)(v & 0xFF);
                        data[off++] = (uint8_t)(v >> 8);
                    }
                }
                raw_hid_send(data, length);
                break;
            }
            case 7: //lang
                memset(data, 0, length);
                switch(local_state->lang) {
                    /*[[[cog
                    for lang in languages:
                        cog.outl(f'case LANG_{lang.upper()}: memcpy(data, "P\\x07.{lang}", 7); break;')
                    ]]]*/
                    case LANG_ENUS: memcpy(data, "P\x07.enUS", 7); break;
                    case LANG_DEDE: memcpy(data, "P\x07.deDE", 7); break;
                    case LANG_FRFR: memcpy(data, "P\x07.frFR", 7); break;
                    case LANG_ESES: memcpy(data, "P\x07.esES", 7); break;
                    case LANG_PTPT: memcpy(data, "P\x07.ptPT", 7); break;
                    case LANG_ITIT: memcpy(data, "P\x07.itIT", 7); break;
                    case LANG_TRTR: memcpy(data, "P\x07.trTR", 7); break;
                    case LANG_KOKR: memcpy(data, "P\x07.koKR", 7); break;
                    case LANG_JAJP: memcpy(data, "P\x07.jaJP", 7); break;
                    case LANG_ARSA: memcpy(data, "P\x07.arSA", 7); break;
                    case LANG_ELGR: memcpy(data, "P\x07.elGR", 7); break;
                    case LANG_UKUA: memcpy(data, "P\x07.ukUA", 7); break;
                    case LANG_RURU: memcpy(data, "P\x07.ruRU", 7); break;
                    case LANG_BEBY: memcpy(data, "P\x07.beBY", 7); break;
                    case LANG_KKKZ: memcpy(data, "P\x07.kkKZ", 7); break;
                    case LANG_BGBG: memcpy(data, "P\x07.bgBG", 7); break;
                    case LANG_PLPL: memcpy(data, "P\x07.plPL", 7); break;
                    case LANG_RORO: memcpy(data, "P\x07.roRO", 7); break;
                    case LANG_ZHCN: memcpy(data, "P\x07.zhCN", 7); break;
                    case LANG_NLNL: memcpy(data, "P\x07.nlNL", 7); break;
                    case LANG_HEIL: memcpy(data, "P\x07.heIL", 7); break;
                    case LANG_SVSE: memcpy(data, "P\x07.svSE", 7); break;
                    case LANG_FIFI: memcpy(data, "P\x07.fiFI", 7); break;
                    case LANG_NNNO: memcpy(data, "P\x07.nnNO", 7); break;
                    case LANG_DADK: memcpy(data, "P\x07.daDK", 7); break;
                    case LANG_HUHU: memcpy(data, "P\x07.huHU", 7); break;
                    case LANG_CSCZ: memcpy(data, "P\x07.csCZ", 7); break;
                    case LANG_HRHR: memcpy(data, "P\x07.hrHR", 7); break;
                    case LANG_SKSK: memcpy(data, "P\x07.skSK", 7); break;
                    case LANG_LTLT: memcpy(data, "P\x07.ltLT", 7); break;
                    case LANG_LVLV: memcpy(data, "P\x07.lvLV", 7); break;
                    case LANG_ETEE: memcpy(data, "P\x07.etEE", 7); break;
                    case LANG_PTBR: memcpy(data, "P\x07.ptBR", 7); break;
                    case LANG_SRRS: memcpy(data, "P\x07.srRS", 7); break;
                    case LANG_MKMK: memcpy(data, "P\x07.mkMK", 7); break;
                    case LANG_FAIR: memcpy(data, "P\x07.faIR", 7); break;
                    case LANG_HIIN: memcpy(data, "P\x07.hiIN", 7); break;
                    case LANG_MRIN: memcpy(data, "P\x07.mrIN", 7); break;
                    case LANG_NENP: memcpy(data, "P\x07.neNP", 7); break;
                    case LANG_MNMN: memcpy(data, "P\x07.mnMN", 7); break;
                    case LANG_URPK: memcpy(data, "P\x07.urPK", 7); break;
                    case LANG_ENGB: memcpy(data, "P\x07.enGB", 7); break;
                    case LANG_ESMX: memcpy(data, "P\x07.esMX", 7); break;
                    case LANG_DECH: memcpy(data, "P\x07.deCH", 7); break;
                    case LANG_FRBE: memcpy(data, "P\x07.frBE", 7); break;
                    case LANG_FRCA: memcpy(data, "P\x07.frCA", 7); break;
                    case LANG_THTH: memcpy(data, "P\x07.thTH", 7); break;
                    case LANG_BNIN: memcpy(data, "P\x07.bnIN", 7); break;
                    case LANG_TEIN: memcpy(data, "P\x07.teIN", 7); break;
                    case LANG_TAIN: memcpy(data, "P\x07.taIN", 7); break;
                    case LANG_ZHTW: memcpy(data, "P\x07.zhTW", 7); break;
                    case LANG_KAGE: memcpy(data, "P\x07.kaGE", 7); break;
                    case LANG_HYAM: memcpy(data, "P\x07.hyAM", 7); break;
                    case LANG_IDID: memcpy(data, "P\x07.idID", 7); break;
                    case LANG_AZAZ: memcpy(data, "P\x07.azAZ", 7); break;
                    case LANG_ISIS: memcpy(data, "P\x07.isIS", 7); break;
                    case LANG_VIVN: memcpy(data, "P\x07.viVN", 7); break;
                    case LANG_ZHHK: memcpy(data, "P\x07.zhHK", 7); break;
                    case LANG_ENAU: memcpy(data, "P\x07.enAU", 7); break;
                    case LANG_ENNZ: memcpy(data, "P\x07.enNZ", 7); break;
                    case LANG_MINZ: memcpy(data, "P\x07.miNZ", 7); break;
                    case LANG_SMWS: memcpy(data, "P\x07.smWS", 7); break;
                    case LANG_FJFJ: memcpy(data, "P\x07.fjFJ", 7); break;
                    case LANG_TLPH: memcpy(data, "P\x07.tlPH", 7); break;
                    case LANG_HWUS: memcpy(data, "P\x07.hwUS", 7); break;
                    case LANG_ENZA: memcpy(data, "P\x07.enZA", 7); break;
                    case LANG_AFZA: memcpy(data, "P\x07.afZA", 7); break;
                    case LANG_AREG: memcpy(data, "P\x07.arEG", 7); break;
                    case LANG_SWKE: memcpy(data, "P\x07.swKE", 7); break;
                    case LANG_AMET: memcpy(data, "P\x07.amET", 7); break;
                    case LANG_YONG: memcpy(data, "P\x07.yoNG", 7); break;
                    case LANG_ENNG: memcpy(data, "P\x07.enNG", 7); break;
                    case LANG_ARMA: memcpy(data, "P\x07.arMA", 7); break;
                    case LANG_ARIQ: memcpy(data, "P\x07.arIQ", 7); break;
                    case LANG_KUIQ: memcpy(data, "P\x07.kuIQ", 7); break;
                    case LANG_MSMY: memcpy(data, "P\x07.msMY", 7); break;
                    case LANG_UZUZ: memcpy(data, "P\x07.uzUZ", 7); break;
                    case LANG_ENCA: memcpy(data, "P\x07.enCA", 7); break;
                    case LANG_ESAR: memcpy(data, "P\x07.esAR", 7); break;
                    case LANG_ENPG: memcpy(data, "P\x07.enPG", 7); break;
                    case LANG_TYPF: memcpy(data, "P\x07.tyPF", 7); break;
                    case LANG_ESCO: memcpy(data, "P\x07.esCO", 7); break;
                    case LANG_ESPE: memcpy(data, "P\x07.esPE", 7); break;
                    case LANG_ESVE: memcpy(data, "P\x07.esVE", 7); break;
                    case LANG_ESCL: memcpy(data, "P\x07.esCL", 7); break;
                    case LANG_ESEC: memcpy(data, "P\x07.esEC", 7); break;
                    case LANG_ESGT: memcpy(data, "P\x07.esGT", 7); break;
                    case LANG_ESDO: memcpy(data, "P\x07.esDO", 7); break;
                    case LANG_ESBO: memcpy(data, "P\x07.esBO", 7); break;
                    case LANG_ESPY: memcpy(data, "P\x07.esPY", 7); break;
                    case LANG_ESCR: memcpy(data, "P\x07.esCR", 7); break;
                    case LANG_ESSV: memcpy(data, "P\x07.esSV", 7); break;
                    case LANG_ESHN: memcpy(data, "P\x07.esHN", 7); break;
                    case LANG_ESPA: memcpy(data, "P\x07.esPA", 7); break;
                    case LANG_ESUY: memcpy(data, "P\x07.esUY", 7); break;
                    case LANG_ESNI: memcpy(data, "P\x07.esNI", 7); break;
                    case LANG_DEAT: memcpy(data, "P\x07.deAT", 7); break;
                    case LANG_NLBE: memcpy(data, "P\x07.nlBE", 7); break;
                    case LANG_CAES: memcpy(data, "P\x07.caES", 7); break;
                    case LANG_ENIE: memcpy(data, "P\x07.enIE", 7); break;
                    case LANG_BSBA: memcpy(data, "P\x07.bsBA", 7); break;
                    case LANG_FRCH: memcpy(data, "P\x07.frCH", 7); break;
                    case LANG_SLSI: memcpy(data, "P\x07.slSI", 7); break;
                    case LANG_FOFO: memcpy(data, "P\x07.foFO", 7); break;
                    case LANG_ARAE: memcpy(data, "P\x07.arAE", 7); break;
                    case LANG_ARSY: memcpy(data, "P\x07.arSY", 7); break;
                    case LANG_ARJO: memcpy(data, "P\x07.arJO", 7); break;
                    case LANG_ARLB: memcpy(data, "P\x07.arLB", 7); break;
                    case LANG_ARYE: memcpy(data, "P\x07.arYE", 7); break;
                    case LANG_ARKW: memcpy(data, "P\x07.arKW", 7); break;
                    case LANG_AROM: memcpy(data, "P\x07.arOM", 7); break;
                    case LANG_ARPS: memcpy(data, "P\x07.arPS", 7); break;
                    case LANG_ARQA: memcpy(data, "P\x07.arQA", 7); break;
                    case LANG_ARBH: memcpy(data, "P\x07.arBH", 7); break;
                    case LANG_ARDZ: memcpy(data, "P\x07.arDZ", 7); break;
                    case LANG_ARSD: memcpy(data, "P\x07.arSD", 7); break;
                    case LANG_ARTN: memcpy(data, "P\x07.arTN", 7); break;
                    case LANG_ARLY: memcpy(data, "P\x07.arLY", 7); break;
                    case LANG_FRCD: memcpy(data, "P\x07.frCD", 7); break;
                    case LANG_FRCI: memcpy(data, "P\x07.frCI", 7); break;
                    case LANG_FRCM: memcpy(data, "P\x07.frCM", 7); break;
                    case LANG_FRSN: memcpy(data, "P\x07.frSN", 7); break;
                    case LANG_FRMG: memcpy(data, "P\x07.frMG", 7); break;
                    case LANG_ENGH: memcpy(data, "P\x07.enGH", 7); break;
                    case LANG_ENUG: memcpy(data, "P\x07.enUG", 7); break;
                    case LANG_ENZM: memcpy(data, "P\x07.enZM", 7); break;
                    case LANG_SWTZ: memcpy(data, "P\x07.swTZ", 7); break;
                    case LANG_PTAO: memcpy(data, "P\x07.ptAO", 7); break;
                    case LANG_PTMZ: memcpy(data, "P\x07.ptMZ", 7); break;
                    case LANG_BNBD: memcpy(data, "P\x07.bnBD", 7); break;
                    case LANG_ENIN: memcpy(data, "P\x07.enIN", 7); break;
                    case LANG_ENPK: memcpy(data, "P\x07.enPK", 7); break;
                    case LANG_ENPH: memcpy(data, "P\x07.enPH", 7); break;
                    case LANG_ENSG: memcpy(data, "P\x07.enSG", 7); break;
                    case LANG_ENLK: memcpy(data, "P\x07.enLK", 7); break;
                    case LANG_KYKG: memcpy(data, "P\x07.kyKG", 7); break;
                    case LANG_TGTJ: memcpy(data, "P\x07.tgTJ", 7); break;
                    case LANG_ENGU: memcpy(data, "P\x07.enGU", 7); break;
                    case LANG_ENSB: memcpy(data, "P\x07.enSB", 7); break;
                    case LANG_ENVU: memcpy(data, "P\x07.enVU", 7); break;
                    case LANG_ENFM: memcpy(data, "P\x07.enFM", 7); break;
                    case LANG_FRNC: memcpy(data, "P\x07.frNC", 7); break;
                    case LANG_TOTO: memcpy(data, "P\x07.toTO", 7); break;
                    case LANG_EUES: memcpy(data, "P\x07.euES", 7); break;
                    case LANG_GLES: memcpy(data, "P\x07.glES", 7); break;
                    case LANG_RMCH: memcpy(data, "P\x07.rmCH", 7); break;
                    case LANG_CYGB: memcpy(data, "P\x07.cyGB", 7); break;
                    case LANG_GAIE: memcpy(data, "P\x07.gaIE", 7); break;
                    case LANG_MTMT: memcpy(data, "P\x07.mtMT", 7); break;
                    case LANG_LBLU: memcpy(data, "P\x07.lbLU", 7); break;
                    case LANG_SENO: memcpy(data, "P\x07.seNO", 7); break;
                    case LANG_GNPY: memcpy(data, "P\x07.gnPY", 7); break;
                    case LANG_QUPE: memcpy(data, "P\x07.quPE", 7); break;
                    case LANG_AYBO: memcpy(data, "P\x07.ayBO", 7); break;
                    case LANG_NVUS: memcpy(data, "P\x07.nvUS", 7); break;
                    case LANG_NHMX: memcpy(data, "P\x07.nhMX", 7); break;
                    case LANG_PSAF: memcpy(data, "P\x07.psAF", 7); break;
                    case LANG_IUCA: memcpy(data, "P\x07.iuCA", 7); break;
                    case LANG_CRCA: memcpy(data, "P\x07.crCA", 7); break;
                    case LANG_CKUS: memcpy(data, "P\x07.ckUS", 7); break;
                    //[[[end]]]
                    default:
                        hid_reply(data, 0x07, false);
                        break;
                }
                raw_hid_send(data, length);
                break;
            case 8: // GET_LANG_LIST (legacy ASCII list) — RETIRED in protocol v2.
                // Superseded by GET_LANG_LIST_PACKED (cmd 27), which sends the
                // same list as compact 2-byte ISO index pairs. Protocol-v2 hosts
                // use cmd 27 exclusively; NACK the old command so any stale host
                // that still asks for the ASCII list fails loudly instead of
                // parsing a missing/partial reply. (Dropping the table here also
                // frees the ~570 bytes of .rodata the ASCII list occupied.)
                memset(data, 0, length);
                hid_reply(data, 0x08, false);
                raw_hid_send(data, length);
                break;
            case 9: //change language
                {
                    uint8_t* start = &data[HID_DATA_IDX];
                    uint32_t decoded = LANG_TO_UI32_ARR(start);
                    uint8_t new_lang = local_state->lang;

                    for(uint8_t idx=0;idx<NUM_LANG;++idx) {
                        if(decode_lang(idx)==decoded) {
                            new_lang = idx;
                            break;
                        }
                    }

                    memset(data, 0, length);
                    if(new_lang<NUM_LANG) {
                        local_state->lang = new_lang;
                        uprintf("Setting lang to %u.\n", new_lang);
                        request_disp_refresh();
                        update_performed();
                        hid_reply(data, 0x09, true);
                    } else {
                        uprintf("Invalid language index %u.\n", new_lang);
                        hid_reply(data, 0x09, false);
                    }
                    raw_hid_send(data, length);
                }
                break;
            case 10: //receive overlay
                {
                    reset_fragment_context();
                    // Protocol 11: modifier (low nibble, 0..8) and segment index
                    // (high nibble, 0..5) share one header byte, so the 60-byte
                    // segment payload starts at HID_DATA_IDX+2 and fits the report
                    // exactly (2 fixed + keycode + packed + 60 = 64). The old
                    // layout put modifier and segment in separate bytes, leaving
                    // only 59 bytes for a 60-byte segment -> a 1-byte over-read.
                    uint8_t packed  = data[HID_DATA_IDX+1];
                    uint8_t segment = packed >> 4;
                    set_fragment_context_key(data[HID_DATA_IDX], packed & 0x0F);
                    if(get_fragment_context()->keycode>=KC_A && get_fragment_context()->keycode<=KC_RIGHT_GUI && segment<NUM_SEGMENTS_PER_OVERLAY) {
                        fill_overlay_buffer(segment, &data[HID_DATA_IDX+2]);
                        if(segment==NUM_SEGMENTS_PER_OVERLAY-1) {
                            // Refresh only — deliberately NOT update_performed().
                            // An overlay upload is the HOST reacting to a window
                            // switch, not the user touching the keyboard; counting
                            // it as activity restarted the whole idle countdown
                            // every time the focused app changed (so a keyboard
                            // watching a busy machine never idled) with nothing in
                            // the log to explain it. See the note in base/update.h.
                            request_disp_refresh();
                        }
                    }
                }
                break;
            case 11: //overlays flags on
                {
                    uint8_t new_flags = data[HID_DATA_IDX];
                    local_state->overlay_flags = flag_on(local_state->overlay_flags, new_flags);
                    apply_overlay_action_flags(new_flags);
                    const bool needs_force_sync =
                        (new_flags & OVERLAY_ACTION_FLAGS) || (new_flags & OVERLAY_SYNCED_STATE_FLAGS);
                    if(needs_force_sync) {
                        if(!sync_succeeded(send_to_bridge(USER_SYNC_POLY_DATA, (void *)local_state, sizeof(poly_sync_t), 10))) {
                            // The ACTION bits (reset buffers/usage/mapping) are cleared
                            // right below and never re-sent, so a give-up here leaves the
                            // slave on the PREVIOUS program's mapping. DISPLAY_OVERLAYS /
                            // MIRROR_OVERLAYS are in OVERLAY_SYNCED_STATE_FLAGS and do get
                            // re-fired by the periodic diff, so only the action bits are
                            // genuinely lost. The enable-time repair below re-pushes the
                            // mapping, which is what actually corrects a stale slave.
                            note_overlay_map_sync_lost();
                            uprintf("Warning: overlay flags 0x%x did not reach the slave; repairing at enable.\n", new_flags);
                        }
                        if(new_flags & OVERLAY_ACTION_FLAGS) {
                            local_state->overlay_flags &= ~OVERLAY_ACTION_FLAGS;
                        }
                        request_disp_refresh();
                    }
                    // End of the host's app-switch sequence (prepare -> images ->
                    // mapping -> enable): if any bridge above dropped, arm a repair
                    // that rebuilds the slave's mapping from our own tables. The
                    // sending itself runs in housekeeping (overlay_map_repair_tick),
                    // NOT here — see the warning in fill_overlay.h.
                    if(new_flags & DISPLAY_OVERLAYS) {
                        arm_overlay_map_repair();
                    }
                    memset(data, 0, length);
                    hid_reply(data, 0x0b, true);
                    if (debug_enable) {
                        uprintf("Overlay flags 0x%x set.\n", new_flags);
                    }
                    raw_hid_send(data, length);
                }
                break;

            case 12: //overlays flags off
                {
                    uint8_t flags_to_clear = data[HID_DATA_IDX];
                    uint8_t old_flags = local_state->overlay_flags;
                    local_state->overlay_flags = flag_off(old_flags, flags_to_clear);
                    // Only sync + full-refresh when a synced-state bit (MIRROR /
                    // DISPLAY overlays) actually TRANSITIONED set->clear. Re-clearing
                    // an already-clear bit changes nothing on screen, so gating on the
                    // real transition avoids a blocking slave bridge-sync + full
                    // 72-keycap re-render for a no-op. A host that re-asserts
                    // "overlays off" on every active-window poll — e.g. a focused
                    // window with no overlays whose TITLE keeps changing (a terminal
                    // animating a spinner in its title) — would otherwise force one
                    // full render per poll. (The host now dedups this too; this is the
                    // firmware-side backstop for any client that re-asserts state.)
                    if(old_flags & flags_to_clear & OVERLAY_SYNCED_STATE_FLAGS) {
                        send_to_bridge(USER_SYNC_POLY_DATA, (void *)local_state, sizeof(poly_sync_t), 10);
                        request_disp_refresh();
                    }
                    memset(data, 0, length);
                    hid_reply(data, 0x0c, true);
                    uprintf("Overlay flags 0x%x cleared.\n", data[HID_DATA_IDX]);
                    raw_hid_send(data, length);
                }
                break;
            case 13: //set brightness
                if ( data[HID_DATA_IDX] <= FULL_BRIGHT) {
                    // data[HID_DATA_IDX]   = brightness level (0..FULL_BRIGHT)
                    // data[HID_DATA_IDX+1] = flags (0 from pre-flag hosts ->
                    //                        plain persisted set, as before).
                    uint8_t br_flags = data[HID_DATA_IDX + 1];
                    if (br_flags & BR_FLAG_AUTO_OFF) {
                        // Leave host-auto; revert to the persisted manual value.
                        // The level byte is ignored on an AUTO_OFF message.
                        set_brightness_auto_mode(false);
                    } else if (br_flags & BR_FLAG_VOLATILE) {
                        if (br_flags & BR_FLAG_AUTO_ON) {
                            set_brightness_auto_mode(true);
                        }
                        // Daylight/auto update: applied only while auto mode
                        // is engaged, never persisted.
                        set_auto_brightness_value(data[HID_DATA_IDX]);
                    } else if (br_flags & BR_FLAG_AUTO_ON) {
                        // Engage auto mode without turning this packet into a
                        // persisted manual write (which would clear auto again).
                        set_brightness_auto_mode(true);
                    } else {
                        // Explicit set (host slider / polyctl): persists and
                        // leaves auto mode, exactly like a keyboard key.
                        set_user_brightness(data[HID_DATA_IDX]);
                    }
                    memset(data, 0, length);
                    hid_reply(data, 0x0d, true);
                    uprintf("Set brightness to: %u (flags 0x%x, auto %u).\n",
                            local_state->contrast, br_flags, get_brightness_auto_mode());
                } else {
                    uprintf("Refused to set brightness to: %u.\n", data[HID_DATA_IDX]);
                    memset(data, 0, length);
                    hid_reply(data, 0x0d, false);
                }
                raw_hid_send(data, length);
                break;
            case 14: //key press
                // SECURITY: this injects a real key event into QMK (action_exec), i.e.
                // the keyboard types into the host's focused app on the host's command —
                // a keystroke-injection primitive. It's a demo/dev feature, so gate it on
                // debug_enable (off by default; unlock with the DB_TOGG key, which needs
                // physical access). When debug is off, NACK instead of injecting.
                if (debug_enable) {
                    uint16_t keycode = ((uint16_t)data[HID_DATA_IDX])<<8 | data[HID_DATA_IDX+1];
                    uint8_t r = 0, c = 0;
                    enum key_split_pos pos = get_split_matrix_pos(keycode, get_highest_layer(local_layer->layer), &r, &c, is_left_side());
                    const bool pressed = data[HID_DATA_IDX+2] == 0;
                    if(pos==POS_NOT_FOUND) {
                        pos = get_split_matrix_pos(keycode, local_layer->def_layer, &r, &c, is_left_side());
                    }
                    // key_has_display(): one key per half has no OLED, so it must
                    // not drive a chip-select. See the split72 header.
                    if (is_on_current_side(pos) && key_has_display(r, c)) {
                        invert_display(r, c, pressed);
                    }

                    if (is_on_other_side(pos)) {
                        const uint8_t data_len = 6;
                        dynamic_keymap_sync_t sync_data;
                        memcpy(&sync_data.commands, data, data_len);
                        send_to_bridge(USER_SYNC_DYNAMIC_KEYMAP_DATA, (void*)&sync_data, sizeof(sync_data.crc32)+data_len, 3);
                    }

                    action_exec(MAKE_KEYEVENT(r, c, pressed));
                    switch_events_poly(r,c, pressed);
                    memset(data, 0, length);
                    hid_reply(data, 0x0e, true);
                } else {
                    memset(data, 0, length);
                    hid_reply(data, 0x0e, false);   // NACK: key injection requires debug mode
                }
                raw_hid_send(data, length);
                break;
            case 15: //start/stop idle
                if(data[HID_DATA_IDX]==0) {
                    // A host "stop idle" is a wake, so it must tear down the DOOM
                    // attract screensaver (IDLE_STYLE_IDDQD) too — exactly as a
                    // keypress wake does via poly_force_wake()/poly_prepare_for_flash().
                    // The attract demo runs with STATUS_DISP_ON SET and DISP_IDLE
                    // CLEARED, so the flag handling below never reaches it; without
                    // this the host cannot stop the screensaver over HID, and while
                    // doom holds the overlay pool a subsequent font/doom re-flash is
                    // refused (hid_fontpack.c FONTPACK_BEGIN gates on !doom_mode_active()).
                    // Self-guards: only an active attract demo is affected; a no-op
                    // inline stub on a non-doom build.
                    doom_screensaver_stop();
                    if((local_state->flags & (STATUS_DISP_ON|DISP_IDLE))==0) {
                        suspend_wakeup_init_kb();
                    } else {
                        if (local_state->flags & DISP_IDLE) {
                            // Contrast is cycling 0-49 during pulsing; restore the active
                            // brightness (host-auto value or user brightness) so
                            // display_wakeup() conditions don't leave the display dark.
                            local_state->contrast = get_active_brightness();
                        }
                        local_state->flags &= ~((uint8_t)DISP_IDLE);
                        local_state->flags |= STATUS_DISP_ON;
                        reset_idle_jitter();   // fresh, centred idle session next time
                        request_disp_refresh();
                        update_performed();
                    }
                    uprint("Stop idle.\n");
                } else {
                    // Backdate the activity timestamp by a full fade-out interval so
                    // the idle fade begins on the next housekeeping pass. Modular
                    // uint32 arithmetic makes this correct even in the first
                    // FADE_OUT_TIME ms after boot — the old signed subtraction
                    // underflowed there, was clamped to 0, and idle never started.
                    backdate_last_update(FADE_OUT_TIME);
                    uprint("Start idle.\n");
                }
                memset(data, 0, length);
                hid_reply(data, 0x0f, true);
                raw_hid_send(data, length);
                break;
            case 16: //receive RLE compressed overlay
                reset_fragment_context();
                set_fragment_context_key(data[HID_DATA_IDX], data[HID_DATA_IDX+1]);
                uprintf("Start with compressed data for keycode 0x%x (modifiers: 0x%x).\n", get_fragment_context()->keycode, get_fragment_context()->modifier);

                //fall through
            case 17: //receive RLE compressed overlay
                {
                    bool first = data[HID_CMD_IDX]==16;
                    if(get_fragment_context()->keycode>=KC_A && get_fragment_context()->keycode<=KC_RIGHT_GUI) {
                        decompress_overlay_buffer(first?&data[HID_DATA_IDX+2]:&data[HID_DATA_IDX], first);
                    }
                }
                break;
            case 18: //start roi overlay
                if (set_fragment_context_from_buffer(&data[HID_DATA_IDX]) == ROI_BOUNDS_CLAMPED) {
                    const overlay_fragment_context_t *fc = get_fragment_context();
                    uprintf("ROI overlay: host sent out-of-bounds ROI for keycode %u — "
                            "clamped to x=%u y=%u xx=%u yy=%u.\n",
                            fc->keycode, fc->roi.x, fc->roi.y, fc->roi.xx, fc->roi.yy);
                }
                //fall through
            case 19: //receive roi overlay
                {
                    bool first = data[HID_CMD_IDX]==18;
                    if(get_fragment_context()->keycode>=KC_A && get_fragment_context()->keycode<=KC_RIGHT_GUI) {
                        fill_roi_overlay_buffer(&data[HID_DATA_IDX], first);
                    }
                }
                break;
            case 20: //set unicode input mode
                switch(data[HID_DATA_IDX]) {
                    case 0: //Linux = 0
                        unicode_input_mode_set_user(UNICODE_MODE_LINUX);
                        memset(data, 0, length);
                        hid_reply(data, 0x14, true);
                        break;
                    case 1: //Mac = 1
                        unicode_input_mode_set_user(UNICODE_MODE_MACOS);
                        memset(data, 0, length);
                        hid_reply(data, 0x14, true);
                        break;
                    case 2: //Windows = 2
                        unicode_input_mode_set_user(UNICODE_MODE_WINDOWS);
                        memset(data, 0, length);
                        hid_reply(data, 0x14, true);
                        break;
                    case 3: //WinCompose = 3
                        unicode_input_mode_set_user(UNICODE_MODE_WINCOMPOSE);
                        memset(data, 0, length);
                        hid_reply(data, 0x14, true);
                        break;
                    case 4: //BSD = 4
                        unicode_input_mode_set_user(UNICODE_MODE_BSD);
                        memset(data, 0, length);
                        hid_reply(data, 0x14, true);
                        break;
                    default:
                        memset(data, 0, length);
                        hid_reply(data, 0x14, false);
                        break;
                }
                raw_hid_send(data, length);
                break;
            case 21: //receive overlay mapping
                {
                    // Deliberately no ACK (protocol v3+): like the other bulk
                    // overlay commands (10, 16/17, 18/19) this is fire-and-forget.
                    // The old per-chunk ACK was sent only after the blocking UART
                    // bridge below, so it arrived hundreds of ms late and the host
                    // discarded it unread — leftover ACKs poisoned the reply
                    // stream of later commands. Ordering for enable_overlays
                    // (case 11) still holds: reports are dispatched sequentially
                    // and the bridge completes before this case returns.
                    overlay_map_sync_t map_sync;
                    map_sync.width = OVERLAY_MAP_IDX_BITS;   // cmd 21 is fixed 10-bit
                    map_sync.bytes = HID_DATA_MAX;
                    memcpy(map_sync.mapping, &data[HID_DATA_IDX], HID_DATA_MAX);
                    // A chunk is one-shot — nothing re-fires it (unlike the periodic
                    // state syncs, where the diff IS the retry queue). Losing one used
                    // to blank exactly the display positions it carried on the slave,
                    // silently, until the host's next full mapping send. Latch it so
                    // enable_overlays (case 11) rebuilds the slave's mapping from our
                    // own tables — we applied this chunk locally either way.
                    if (!sync_succeeded(send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, (void*)&map_sync,
                                                       sizeof(overlay_map_sync_t), 10))) {
                        note_overlay_map_sync_lost();
                        uprint("Warning: overlay mapping chunk did not reach the slave; repairing at enable.\n");
                    }
                    // Render only if this chunk remapped a position that is on screen;
                    // an all-off-screen chunk (non-held variants, off-layer keys) is
                    // staged silently and shown by the enable-overlays refresh.
                    if (set_packed_overlay_mapping(&data[HID_DATA_IDX], HID_DATA_MAX,
                                                   OVERLAY_MAP_IDX_BITS)) {
                        request_disp_refresh();
                    }
                    // Routine per-chunk chatter — set_packed_overlay_mapping already
                    // logs the decoded pairs under the same gate.
                    if (debug_enable) {
                        uprint("Overlay mapping data received.\n");
                    }
                }
                break;
            case 33: // receive overlay mapping, host-chosen value width (v12+)
                {
                    // Same stream as cmd 21, but data[2] carries the WIDTH so the host
                    // can send each group of pairs at the narrowest width it fits in:
                    // 8 bits = 30 pairs/report, 9 = 27, 10 = 24, 11 = 22. Pairs are
                    // order-independent (each is a standalone assignment), so the host
                    // partitions them by required width rather than by index order —
                    // variants 0..10 keep riding the dense 10-bit form and only the
                    // high GUI combos pay for 11. Silent, like cmd 21.
                    const uint8_t width = data[HID_DATA_IDX];
                    overlay_map_sync_t map_sync;
                    map_sync.width = width;
                    map_sync.bytes = OVERLAY_MAP_W_BYTES;
                    memcpy(map_sync.mapping, &data[OVERLAY_MAP_W_HDR], OVERLAY_MAP_W_BYTES);
                    if (!sync_succeeded(send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, (void*)&map_sync,
                                                       sizeof(overlay_map_sync_t), 10))) {
                        note_overlay_map_sync_lost();
                        uprint("Warning: overlay mapping chunk did not reach the slave; repairing at enable.\n");
                    }
                    if (set_packed_overlay_mapping(&data[OVERLAY_MAP_W_HDR],
                                                   OVERLAY_MAP_W_BYTES, width)) {
                        request_disp_refresh();
                    }
                    if (debug_enable) {
                        uprintf("Overlay mapping data received (%u-bit).\n", (unsigned)width);
                    }
                }
                break;
            case 22: // get default layer
                memset(data, 0, length);
                hid_reply(data, 0x16, true);
                data[3] = (uint8_t)local_layer->def_layer;
                raw_hid_send(data, length);
                break;
            case 23: // enter bootloader (host-triggered; mirrors a QK_BOOTLOADER press)
                uprint("Host requested bootloader.\n");
                poly_announce_bootloader();
                memset(data, 0, length);
                hid_reply(data, 0x17, true);
                raw_hid_send(data, length);
                reset_keyboard();
                break;
            case 24: //display off
                poly_suspend();
                sync_and_refresh_displays();
                // Treat host display-off (e.g. system going to sleep) as a cue to
                // flush all dirty user state to EEPROM (dirty-gated, so cheap).
                save_all_dirty();
                disable_idle_tracking();
                memset(data, 0, length);
                hid_reply(data, 0x18, true);
                raw_hid_send(data, length);
                break;
            case 25: //set handedness (which half is left / which is right)
                {
                    bool master_is_left = (data[HID_DATA_IDX] == 0);
                    eeconfig_update_handedness(master_is_left);
                    poly_reset_sync_t msg = { .crc32 = 0, .magic = POLY_RESET_MAGIC,
                                              .action = RESET_ACTION_REBOOT,
                                              .set_handedness = 1, .is_left = master_is_left ? 0 : 1 };
                    uint8_t ack = send_to_bridge(USER_SYNC_RESET, &msg, sizeof(msg), 5);
                    uprintf("Set handedness: master=%s, slave ack=%d.\n", master_is_left ? "LEFT" : "RIGHT", ack);
                    memset(data, 0, length);
                    hid_reply(data, 0x19, true);
                    raw_hid_send(data, length);
                    soft_reset_keyboard();
                }
                break;
            // Flush all dirty user state to EEPROM (host shutdown/suspend signal).
            // Master-only: HID commands run on the USB half. The slave flushes
            // independently via its own suspend hook. (Distinct from KC_STORE_EE,
            // which sets SAVE_EEPROM and syncs that flag to the slave — see base/com.h.)
            case 26:
                save_all_dirty();
                memset(data, 0, length);
                hid_reply(data, 0x1A, true);
                raw_hid_send(data, length);
                break;
            case 27: //packed lang list: 1 count byte + (ISO 639-1 idx, ISO 3166-1 idx) per lang
                memset(data, 0, length);
                /*[[[cog
                import sys
                sys.path.insert(0, os.path.join(os.path.dirname(cog.inFile), "lang"))
                from iso_lang_country import encode_pair
                # Payload: count, then 2 index bytes per language (see lang/iso_lang_country.py).
                packed = [len(languages)]
                for lang in languages:
                    packed += list(encode_pair(lang))
                CHUNK = RAW_EPSIZE - 3  # 61 payload bytes after the 3-byte "P\x1b." header (RAW_EPSIZE from the top cog block)
                for off in range(0, len(packed), CHUNK):
                    seg = packed[off:off+CHUNK]
                    # All bytes emitted as \xNN (incl. header) so no literal char ever
                    # follows a hex escape -> avoids C's greedy \x escape swallowing it.
                    lit = "\\x50\\x1b\\x2e" + "".join(f"\\x{b:02x}" for b in seg)
                    cog.outl(f'memcpy(data, "{lit}", {3 + len(seg)});')
                    cog.outl('raw_hid_send(data, length);')
                    if off + CHUNK < len(packed):
                        cog.outl('memset(data, 0, length);')
                ]]]*/
                memcpy(data, "\x50\x1b\x2e\xa0\x25\xe8\x20\x38\x2f\x4a\x27\x43\x82\xb7\x48\x6d\xa4\xe0\x54\x79\x4a\x71\x07\xc0\x24\x58\xaa\xe5\x87\xbe\x0d\x23\x50\x7c\x0e\x15\x80\xb2\x86\xbc\xb6\x2f\x73\xa5\x38\x66\x99\xc4\x2c\x45\x74\xa6\x1f\x3a\x3d\x63\x1b\x37\x3b\x61\x8f\xc9\x61\x84", 64);
                raw_hid_send(data, length);
                memset(data, 0, length);
                memcpy(data, "\x50\x1b\x2e\x63\x86\x28\x3f\x82\x1e\x95\xbd\x67\x8f\x2a\x6b\x39\x68\x6a\x68\x71\xa7\x69\x92\xab\xb1\x25\x4c\x27\x9c\x20\x2a\x2f\x13\x2f\x25\x9e\xd9\x12\x68\x9c\x68\x9b\x68\xb6\xe3\x4c\x4e\x3e\x06\x41\x64\x0b\x0f\x47\x6c\xae\xf0\xb6\x5e\x25\x0c\x25\xaa\x66", 64);
                raw_hid_send(data, length);
                memset(data, 0, length);
                memcpy(data, "\x50\x1b\x2e\xaa\x91\xf3\x2d\x46\xa1\xb0\xb8\xe8\x25\xf6\x03\xf6\x07\x40\x9a\x72\x05\x44\xb4\xa3\x25\xa3\x07\x88\x07\x6a\x57\x6a\x6b\x9d\xac\xea\x25\x25\x27\x09\x25\xaf\xa8\xae\x27\x30\x27\xad\x27\xed\x27\x2d\x27\x3e\x27\x5a\x27\x3c\x27\x1c\x27\xb9\x27\x31", 64);
                raw_hid_send(data, length);
                memset(data, 0, length);
                memcpy(data, "\x50\x1b\x2e\x27\xd1\x27\x60\x27\xac\x27\xe9\x27\xa4\x20\x0b\x73\x13\x16\x43\x25\x65\x15\x10\x2f\x2a\x90\xc7\x2e\x49\x07\x01\x07\xd3\x07\x70\x07\x7e\x07\xf4\x07\x7a\x07\xab\x07\xb6\x07\xba\x07\x16\x07\x3d\x07\xc3\x07\xde\x07\x87\x2f\x27\x2f\x2b\x2f\x2e\x2f", 64);
                raw_hid_send(data, length);
                memset(data, 0, length);
                memcpy(data, "\x50\x1b\x2e\xcc\x2f\x8d\x25\x51\x25\xe6\x25\xf7\x9a\xe4\x82\x07\x82\x9e\x12\x12\x25\x68\x25\xb1\x25\xb0\x25\xc5\x25\x81\x5a\x73\x9d\xda\x25\x5b\x25\xc1\x25\xf1\x25\x48\x2f\xa0\xa3\xdf\x29\x43\x33\x43\x84\x2a\x1e\x4c\x31\x65\x6c\x98\x5c\x85\x8c\xa6\x34\xb9", 64);
                raw_hid_send(data, length);
                memset(data, 0, length);
                memcpy(data, "\x50\x1b\x2e\x83\xad\x0a\x1c\x77\xe8\xb9\x9c\x81\x02\x49\x25\x1a\x25\xba\xe8", 19);
                raw_hid_send(data, length);
                //[[[end]]]
                break;
            case 28: //get/set idle (anti-burn-in) display style (protocol v4+)
                {
                    // data[HID_DATA_IDX] == 0xFF -> query (reply current style in data[3]).
                    // Otherwise set the style (0 = pulse, 1 = jitter, 2 = doom attract
                    // screensaver — falls back to pulse at runtime when the demo can't
                    // start); persisted at the next EEPROM flush (suspend / store key).
                    uint8_t arg = data[HID_DATA_IDX];
                    memset(data, 0, length);
                    if (arg == 0xFF) {
                        hid_reply(data, 0x1c, true);
                        data[3] = get_idle_style();
                    } else if (arg < IDLE_STYLE_COUNT) {
                        set_idle_style(arg);
                        // The style is synced to the slave from housekeeping; both
                        // halves relocate their own keys, so there is no shared
                        // offset to reset here.
                        hid_reply(data, 0x1c, true);
                        data[3] = arg;
                        uprintf("Set idle style to %u.\n", arg);
                    } else {
                        hid_reply(data, 0x1c, false);
                        uprintf("Refused idle style %u.\n", arg);
                    }
                    raw_hid_send(data, length);
                }
                break;
            case 29: //get/set active host-OS identity (protocol v7+)
                {
                    // data[HID_DATA_IDX]   = arg:
                    //   0xFF -> query (reply data[3] = active OS, data[4] = auto-mode flag)
                    //   0xFE -> engage auto mode (clear the manual pin; detection+host resume)
                    //   else -> set the OS (enum poly_os); data[HID_DATA_IDX+1] flags:
                    //           bit0 = 1 -> manual PIN (wins, persists); 0 -> host-auto push.
                    // The OS is its own state, independent of the unicode mode (cmd 20):
                    // it drives the modifier-legend swap, the OS icon, and the semantic
                    // action keys. Synced to the slave from housekeeping (active_os).
                    uint8_t arg   = data[HID_DATA_IDX];
                    uint8_t flags = data[HID_DATA_IDX + 1];
                    memset(data, 0, length);
                    if (arg == 0xFF) {
                        hid_reply(data, 0x1d, true);
                        data[3] = get_active_os();
                        data[4] = get_os_auto_mode() ? 1 : 0;
                    } else if (arg == 0xFE) {
                        set_os_auto_mode(true);
                        hid_reply(data, 0x1d, true);
                        data[3] = get_active_os();
                        data[4] = 1;
                        uprint("OS auto mode engaged.\n");
                    } else if (arg < POLY_OS_COUNT) {
                        if (flags & 0x01) {
                            set_user_os(arg);     // manual pin
                        } else {
                            set_host_os(arg);     // host-auto push
                        }
                        hid_reply(data, 0x1d, true);
                        data[3] = get_active_os();
                        data[4] = get_os_auto_mode() ? 1 : 0;
                        uprintf("Set OS to %u (pin=%u), active=%u.\n", arg, (unsigned)(flags & 0x01), data[3]);
                    } else {
                        hid_reply(data, 0x1d, false);
                        uprintf("Refused OS %u.\n", arg);
                    }
                    raw_hid_send(data, length);
                }
                break;
            case 30: //get/set glyph-script override (protocol v9+)
                {
                    // data[HID_DATA_IDX] == 0xFF -> query (reply current script in data[3]).
                    // Otherwise set the script by INDEX (0 = standard/off, 1 = Tengwar,
                    // 2.. = the expanded scripts). Any index 0..0xFE is ACCEPTED even if
                    // this firmware doesn't know it or its font isn't flashed — the
                    // renderer just falls back to the normal legend (glyph_script_codepoint
                    // returns 0 for unknown indices). This is what decouples "add a font
                    // face" from the protocol version: the host may offer more scripts than
                    // a given keyboard has, and older keyboards degrade gracefully instead
                    // of NACKing. The override replaces the language-layer letter/digit
                    // legends only and is persisted at the next EEPROM flush (suspend /
                    // store key). The awake re-render + slave sync run from housekeeping.
                    uint8_t arg = data[HID_DATA_IDX];
                    memset(data, 0, length);
                    if (arg == 0xFF) {
                        hid_reply(data, 0x1e, true);
                        data[3] = get_glyph_script();
                    } else {
                        set_glyph_script(arg);
                        hid_reply(data, 0x1e, true);
                        data[3] = arg;
                        uprintf("Set glyph script to %u.\n", arg);
                    }
                    raw_hid_send(data, length);
                }
                break;
            case 31: //replay the startup ("Eden") animation on demand
                {
                    // Fun/debug: re-play the one-time boot animation without a
                    // power cycle. Start it on this (master) half and bump the
                    // synced nonce so the slave replays too (see split_sync.c).
                    startup_anim_start();
                    access_local_state()->anim_nonce++;
                    memset(data, 0, length);
                    hid_reply(data, 0x1f, true);
                    raw_hid_send(data, length);
                    uprint("Replaying startup animation.\n");
                }
                break;
            case 34: //get/set keycap legend size (protocol v13+)
                {
                    // data[HID_DATA_IDX] == 0xFF -> query (reply current size in data[3]).
                    // Otherwise set the size (enum poly_glyph_size: 0 = small/original,
                    // 1 = medium, 2 = large). Applies to the key's MAIN legend only —
                    // the shift / AltGr previews and every other kind of chrome stay as
                    // they are. Persisted at the next EEPROM flush (suspend / store key);
                    // the awake re-render + slave sync run from housekeeping.
                    //
                    // ⚠️ The range is CLOSED, unlike the glyph script one command over.
                    // A script index the firmware doesn't know can fall through to the
                    // normal legend, so accepting it costs nothing and buys the host the
                    // freedom to add faces without a protocol bump. A SIZE is different:
                    // every value has to name a tier whose relocation base and baseline
                    // this firmware knows, so an unknown one would be stored, synced and
                    // persisted while rendering as small — a setting that silently does
                    // nothing. NACK it instead, and let the host's feature gate decide.
                    //
                    // Sizes above small need the `latinbig` font-pack bundle. That is NOT
                    // checked here: the bundle can be flashed after the choice is made, so
                    // the fallback lives at draw time (glyph_size_remap), where it is
                    // re-evaluated on every render.
                    uint8_t arg = data[HID_DATA_IDX];
                    memset(data, 0, length);
                    if (arg == 0xFF) {
                        hid_reply(data, 0x22, true);
                        data[3] = get_glyph_size();
                    } else if (arg < GLYPH_SIZE_COUNT) {
                        set_glyph_size(arg);
                        hid_reply(data, 0x22, true);
                        data[3] = arg;
                        uprintf("Set glyph size to %s.\n", glyph_size_name(arg));
                    } else {
                        hid_reply(data, 0x22, false);
                        uprintf("Refused glyph size %u.\n", arg);
                    }
                    raw_hid_send(data, length);
                }
                break;
            case 35: //report the host-remappable layers and their names (protocol v14+)
                {
                    // Reply: "P\x23." then the payload
                    //
                    //     [0] total   whole payload length, THIS BYTE INCLUDED
                    //     [1] count   number of layers named
                    //     [2..] count NUL-terminated ASCII names, <= POLY_LAYER_NAME_MAX each
                    //
                    // split across as many reports as it needs (54 bytes / one report today).
                    //
                    // ⚠️ The TOTAL is what makes this decodable, and it is why the length
                    // does not live in the records. The host reads byte 0, keeps reading
                    // until it holds that many bytes, and only then splits on the NULs —
                    // so termination is arithmetic rather than a scan, and the zero fill
                    // past the payload is never examined. Two encodings were tried first
                    // and both are worse:
                    //   - fixed-width 8-byte records also give an arithmetic length, but
                    //     cost 65 bytes and therefore a second report;
                    //   - terminated records with no total force the decoder to scan for
                    //     the end, and the only way to tell a real terminator from the
                    //     report's zero fill is "an empty name means padding" — which
                    //     makes an UNNAMED layer (poly_layer_name_wire returning NULL,
                    //     i.e. a bare terminator) indistinguishable from the fill and
                    //     silently truncates the list.
                    //
                    // The count is DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT — the SAME value
                    // id_dynamic_keymap_get_layer_count already answers with, on purpose:
                    // the editor sizes its tab strip from that command and labels the tabs
                    // from this one, so two different counts would let it draw a tab it has
                    // no name for. Layers at or above the cap are served from flash and
                    // cannot be remapped, so they get no name.
                    //
                    // This exists because the names used to be a build-time artifact on the
                    // host, generated from layers.h by a script whose source path had gone
                    // stale — so the editor labelled tabs from an enum the firmware had not
                    // had for a long time. A name the keyboard states itself cannot drift.
                    // The payload itself is built by poly_layer_names_payload()
                    // (layer_names.c, unit-tested) — this case only chunks it into
                    // reports.
                    uint8_t        payload[LAYER_NAMES_PAYLOAD_MAX];
                    const uint16_t used  = poly_layer_names_payload(payload);
                    const uint16_t chunk = hid_payload_avail(length, 3);
                    for (uint16_t off = 0; off < used; off += chunk) {
                        const uint16_t n = (used - off < chunk) ? (uint16_t)(used - off) : chunk;
                        memset(data, 0, length);
                        hid_reply(data, 0x23, true);
                        memcpy(&data[3], &payload[off], n);
                        raw_hid_send(data, length);
                    }
                }
                break;
            case 36: //macro info: how many, how big, how much is in use (protocol v15+)
                {
                    // Read-only. The host needs all four before it can lay out an editor:
                    // the count and the label stride are compile-time, the capacity is
                    // whatever survived the EEPROM layout, and `used` is what makes the
                    // shared-storage bar honest -- the bodies share one buffer, so a long
                    // macro takes room from the others and that has to be visible before
                    // a save fails rather than after.
                    memset(data, 0, length);
                    hid_reply(data, 0x24, true);
                    data[3] = POLY_MACRO_COUNT;
                    data[4] = POLY_MACRO_LABEL_LEN;
                    uint16_t cap  = poly_macro_capacity();
                    uint16_t used = poly_macro_bytes_used();
                    data[5] = (uint8_t)(cap & 0xFF);
                    data[6] = (uint8_t)(cap >> 8);
                    data[7] = (uint8_t)(used & 0xFF);
                    data[8] = (uint8_t)(used >> 8);
                    // How many keycap styles this firmware can actually DRAW. The host
                    // may offer more (an unknown style degrades to the index rather
                    // than being refused), but a menu that lists only what the board
                    // renders is the honest one -- and this costs a byte in a report
                    // with 55 spare.
                    data[9] = POLY_MACRO_STYLE_COUNT;
                    raw_hid_send(data, length);
                }
                break;
            case 37: //macro body read/write, windowed (protocol v15+)
                {
                    // data[2] = 0 read / 1 write, data[3..4] = offset LE, data[5] = count,
                    // data[6..] = bytes. Windowed rather than whole-macro because the
                    // buffer is up to ~2 KB and a report holds 64 -- the host streams it
                    // the same way it streams an overlay.
                    const uint8_t  header = 6;
                    uint8_t        sub    = data[HID_DATA_IDX];
                    uint16_t       offset = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
                    uint16_t       want   = data[5];
                    const uint16_t avail  = hid_payload_avail(length, header);
                    if (want > avail) want = avail;

                    if (sub == 0) {
                        uint8_t buf[64];
                        poly_macro_read(offset, (uint16_t)want, buf);
                        memset(data, 0, length);
                        hid_reply(data, 0x25, true);
                        data[3] = (uint8_t)want;
                        memcpy(&data[header], buf, want);
                        raw_hid_send(data, length);
                    } else if (sub == 1) {
                        // A write lands mid-buffer, so a macro that is being replaced
                        // is briefly inconsistent -- and an interrupted upload would
                        // otherwise leave a playable splice of the new text and
                        // whatever preceded it. poly_macro_start() refuses to play a
                        // buffer whose last byte is not NUL; the HOST raises a non-zero
                        // marker there before it streams and clears it with the final
                        // window, so the guard is armed for exactly that window. The
                        // marker is the host's job because only the host knows a write
                        // has begun -- from here every window looks alike.
                        poly_macro_write(offset, (uint16_t)want, &data[header]);
                        memset(data, 0, length);
                        hid_reply(data, 0x25, true);
                        raw_hid_send(data, length);
                    } else {
                        // Anything else NACKs rather than falling into the write. The
                        // `else` used to catch every value, so a malformed or newer
                        // client silently modified macro EEPROM instead of being told
                        // the sub-command means nothing here (CodeRabbit, 2026-08-27).
                        memset(data, 0, length);
                        hid_reply(data, 0x25, false);
                        raw_hid_send(data, length);
                    }
                }
                break;
            case 38: //macro look get/set (protocol v15+)
                {
                    // The whole appearance of one macro in one exchange:
                    //   data[2]    macro id
                    //   data[3]    0xFF query, else the caption byte count
                    //   data[4]    style (POLY_MACRO_STYLE_*)
                    //   data[5..8] icon codepoint, little-endian
                    //   data[9..]  caption text
                    // The reply mirrors the same layout.
                    //
                    // One command rather than three, because a macro keycap composes
                    // the caption WITH the style and the icon -- splitting them lets a
                    // host apply half an appearance, and the keycap then draws a
                    // combination the user never asked for until the next write lands.
                    //
                    // ASCII only: poly_macro_look_set drops anything the _Nano_ face
                    // cannot draw, so what is stored is what the keycap will show. An
                    // unknown style is stored as STYLE_INDEX rather than refused -- see
                    // poly_macro.h.
                    const uint8_t header = 9;
                    uint8_t       id     = data[HID_DATA_IDX];
                    uint8_t       n      = data[3];
                    if (id >= POLY_MACRO_COUNT) {
                        memset(data, 0, length);
                        hid_reply(data, 0x26, false);
                        raw_hid_send(data, length);
                        break;
                    }
                    if (n != 0xFF) {
                        const uint16_t avail = hid_payload_avail(length, header);
                        if (n > avail) n = (uint8_t)avail;
                        if (n > POLY_MACRO_LABEL_LEN) n = POLY_MACRO_LABEL_LEN;
                        poly_macro_look_t look;
                        look.style = data[4];
                        look.icon  = (uint32_t)data[5] | ((uint32_t)data[6] << 8)
                                   | ((uint32_t)data[7] << 16) | ((uint32_t)data[8] << 24);
                        memcpy(look.text, &data[header], n);
                        look.text[n] = '\0';
                        poly_macro_look_set(id, &look);
                        request_disp_refresh();
                    }
                    poly_macro_look_t look;
                    poly_macro_look_get(id, &look);
                    uint8_t len = (uint8_t)strlen(look.text);
                    memset(data, 0, length);
                    hid_reply(data, 0x26, true);
                    data[3] = len;
                    data[4] = look.style;
                    for (uint8_t b = 0; b < POLY_MACRO_ICON_LEN; b++) {
                        data[5 + b] = (uint8_t)((look.icon >> (8 * b)) & 0xFFu);
                    }
                    memcpy(&data[header], look.text, len);
                    raw_hid_send(data, length);
                }
                break;
            case 39: //crash record: read the last crash / clear the archive (protocol v16+)
                {
                    // data[HID_DATA_IDX]: 0 = this half's archived crash record,
                    //                     1 = the slave's record as last pulled over the link,
                    //                     2 = clear (erase the flash archive, forget both copies).
                    // Reply "P\x27." then [flags][48-byte poly_crash_record_t]
                    // (crash_record_hid_body): flags bit0 present, bit1 fresh -- the boot
                    // before this one ended in that crash. A clear answers an empty body;
                    // anything else NACKs. The record's wire layout is base/crash_record.h.
                    //
                    // The console line printed with the boot banner is the primary
                    // channel (the host alerts on it); this is how polyctl and the rig
                    // fetch the archive later, and the only way to clear it.
                    const uint8_t which = data[HID_DATA_IDX];
                    if (which > 2) {
                        memset(data, 0, length);
                        hid_reply(data, 0x27, false);
                        raw_hid_send(data, length);
                        break;
                    }
                    if (which == 2) {
                        crash_record_clear();
                        uprint("Crash archive cleared.\n");
                    }
                    memset(data, 0, length);
                    hid_reply(data, 0x27, true);
                    if (which <= 1) {
                        crash_record_hid_body(which, &data[3], (uint8_t)(length - 3));
                    }
                    raw_hid_send(data, length);
                }
                break;
#ifdef POLYKYBD_LOOP_PROFILE
            case 32: //main-loop profiler control (POLYKYBD_LOOP_PROFILE builds only)
                {
                    // Lets an automated harness (the polykybd-ctnd rig) bound ONE
                    // measurement: reset the counters, drive a workload, read the
                    // window back as binary. Without it the only readout is the
                    // periodic console block, whose counters are cumulative from
                    // boot and whose `worst` is an all-time maximum — impossible to
                    // attribute to a specific workload.
                    //
                    // NOT protocol-gated and bumps NO PROTOCOL_VERSION — like cmd 31
                    // (REPLAY_ANIM) and the fontpack commands, it is dispatched
                    // independently. On a normal (non-profiling) build the whole case
                    // is compiled out, so the command NACKs via the default branch;
                    // that NACK is how the rig distinguishes "no profiler in this
                    // firmware" from a real answer.
                    //
                    // data[2] = sub-command, data[3] = page (READ only).
                    const uint8_t sub  = hid_payload_avail(length, 2) > 0 ? data[2] : 0xFFu;
                    const uint8_t page = hid_payload_avail(length, 3) > 0 ? data[3] : 0;
                    switch (sub) {
                        case 0: // RESET — zero the counters and start a fresh window
                            loop_profile_reset();
                            memset(data, 0, length);
                            hid_reply(data, 32, true);
                            raw_hid_send(data, length);
                            break;
                        case 1: { // READ — binary snapshot of the current window
                            uint8_t body[64];
                            uint8_t n = loop_profile_snapshot(page, body, (uint8_t)sizeof(body));
                            memset(data, 0, length);
                            hid_reply(data, 32, n > 0);
                            if (n > 0) {
                                data[3] = page;
                                // Header is 4 bytes (P, cmd, status, page), so the
                                // body is bounded by whatever the report holds after
                                // it — via the shared helper, so a short report can
                                // never underflow into a huge memcpy.
                                const uint16_t room = hid_payload_avail(length, 4);
                                if ((uint16_t)n > room) n = (uint8_t)room;
                                memcpy(&data[4], body, n);
                            }
                            raw_hid_send(data, length);
                            break;
                        }
                        case 2: // LOG — dump the console summary block immediately
                            loop_profile_log_now();
                            memset(data, 0, length);
                            hid_reply(data, 32, true);
                            raw_hid_send(data, length);
                            break;
                        default:
                            memset(data, 0, length);
                            hid_reply(data, 32, false);
                            raw_hid_send(data, length);
                            break;
                    }
                }
                break;
#endif
            default:
                if (hid_fw_up_receive(data, length)) {
                    break;
                }
                if (hid_fontpack_receive(data, length)) {
                    break;
                }
                printf("Unknown command: %u.\n", data[HID_CMD_IDX]);
                data[2] = '!';
                raw_hid_send(data, length);
                break;

        }
    }
    #ifdef DYNAMIC_KEYMAP_ENABLE
    else {
        legacy_command_kb(data, length);
    }
    #endif
}
