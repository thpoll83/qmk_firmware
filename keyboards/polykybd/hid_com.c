// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_com.h"
#include "hid_fw_up.h"
#include "hid_fontpack.h"
#include "split_fw_up.h"

#include QMK_KEYBOARD_H
#include "quantum.h"

#include "raw_hid.h"

#include "state.h"
#include "side.h"
#include "config.h"
#include "split_sync.h"
#include "bridge_helper.h"
#include "matrix_helper.h"
#include "fill_overlay.h"
#include "lang/lang_lut.h"
#include "base/com.h"
#include "base/overlay.h"
#include "base/fontpack.h"
#include "base/update.h"
#include "poly_util.h"

#include <print.h>
#include <transactions.h>
#include <dynamic_keymap.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>


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
// Defined in the shared keyboard-level poly_keymap.c; declared here so the
// shared HID dispatcher can drive the display-off command (case 24).
void poly_suspend(void);
void sync_and_refresh_displays(void);

// Set on boot; cleared after the first GET_ID exchange so the host can detect
// a firmware restart even when it never lost the USB connection.
static bool s_fresh_boot = true;


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

bool legacy_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);
    uint8_t data_len = 0;

    switch(*command_id) {
        case id_dynamic_keymap_reset:
            dynamic_keymap_reset();
            data_len = 1;
            break;
        case id_dynamic_keymap_set_keycode:
            dynamic_keymap_set_keycode_poly(command_data[0], command_data[1], command_data[2], (command_data[3] << 8) | command_data[4]);
            data_len = 6;
            break;
        case id_dynamic_keymap_set_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
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
    const char * name = "P\x06.Split72 " FW_VERSION " P" STR(PROTOCOL_VERSION) " HW" STR(DEVICE_VER) " ";

    if (length<1) {
        return;
    }

    if(data[0] == id_custom_save || data[0] == 'P') {
        const poly_layer_t* local_layer = get_local_layer();
        poly_sync_t* local_state = access_local_state();
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
                    set_fragment_context_key(data[HID_DATA_IDX], data[HID_DATA_IDX+1]);
                    uint8_t segment = data[HID_DATA_IDX+2];
                    if(get_fragment_context()->keycode>=KC_A && get_fragment_context()->keycode<=KC_RIGHT_GUI && segment<NUM_SEGMENTS_PER_OVERLAY) {
                        fill_overlay_buffer(segment, &data[HID_DATA_IDX+3]);
                        if(segment==NUM_SEGMENTS_PER_OVERLAY-1) {
                            update_performed();
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
                        send_to_bridge(USER_SYNC_POLY_DATA, (void *)local_state, sizeof(poly_sync_t), 10);
                        if(new_flags & OVERLAY_ACTION_FLAGS) {
                            local_state->overlay_flags &= ~OVERLAY_ACTION_FLAGS;
                        }
                        request_disp_refresh();
                    }
                    memset(data, 0, length);
                    hid_reply(data, 0x0b, true);
                    uprintf("Overlay flags 0x%x set.\n", new_flags);
                    raw_hid_send(data, length);
                }
                break;

            case 12: //overlays flags off
                {
                    uint8_t flags_to_clear = data[HID_DATA_IDX];
                    local_state->overlay_flags = flag_off(local_state->overlay_flags, flags_to_clear);
                    if(flags_to_clear & OVERLAY_SYNCED_STATE_FLAGS) {
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
                    if (is_on_current_side(pos)) {
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
                    int32_t update = timer_read32() - FADE_OUT_TIME;
                    if(update<0) {
                        uprintf("Starting idle in %ld msec .\n", -update);
                        update=0;
                    } else {
                        uprint("Start idle.\n");
                    }
                    set_last_update(update);
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
                    memcpy(map_sync.mapping, &data[HID_DATA_IDX], HID_DATA_MAX);
                    send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, (void*)&map_sync, sizeof(overlay_map_sync_t), 10);
                    set_10bit_overlay_mapping(&data[HID_DATA_IDX]);
                    request_disp_refresh();
                    uprintf("Overlay mapping data received.\n");
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
                set_last_update(-1);
                memset(data, 0, length);
                hid_reply(data, 0x18, true);
                raw_hid_send(data, length);
                break;
            case 25: //set handedness (which half is left / which is right)
                {
                    bool master_is_left = (data[HID_DATA_IDX] == 0);
                    eeconfig_update_handedness(master_is_left);
                    fw_up_apply_sync_t msg = { .crc32 = 0, .magic = FW_UP_SYNC_MAGIC,
                                               .set_handedness = 1, .is_left = master_is_left ? 0 : 1 };
                    uint8_t ack = send_to_bridge(USER_SYNC_REBOOT, &msg, sizeof(msg), 5);
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
                    // Otherwise set the style (0 = pulse, 1 = jitter); persisted at the
                    // next EEPROM flush (suspend / store key).
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
