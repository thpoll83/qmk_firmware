#include "hid_com.h"

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
#include "base/update.h"

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
            dynamic_keymap_set_keycode(command_data[0], command_data[1], command_data[2], (command_data[3] << 8) | command_data[4]);
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
    const char * name = "P\x06.Split72 " FW_VERSION " HW" STR(DEVICE_VER) " ";

    if (length<1) {
        return;
    }

    if(data[0] == id_custom_save || data[0] == 'P') {
        const poly_layer_t* local_layer = get_local_layer();
        poly_sync_t* local_state = access_local_state();
        switch(data[1]) {
            // case id_custom_channel...id_qmk_led_matrix_channel: //maybe now usable :)
            //     break;
            case 6: //id
                memset(data, 0, length);
                memcpy(data, name, strlen(name));
                if (s_fresh_boot) {
                    data[2] = '*'; // host sees '*' instead of '.' → firmware just booted
                    s_fresh_boot = false;
                }
                raw_hid_send(data, length);
                break;
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
                    //[[[end]]]
                    default:
                        memcpy(data, "P\x07!", 3);
                        break;
                }
                raw_hid_send(data, length);
                break;
            case 8: //lang list
                memset(data, 0, length);
                /*[[[cog
                RAW_EPSIZE = 64
                lang_list = "P\\x08."
                for lang in languages:
                    lang_list += lang
                    if len(lang_list)>=RAW_EPSIZE:
                        cog.outl(f'memcpy(data, "{lang_list}", {len(lang_list)-3});')
                        cog.outl(f'raw_hid_send(data, length);')
                        cog.outl(f'memset(data, 0, length);')
                        lang_list = "P\\x08."
                cog.outl(f'memcpy(data, "{lang_list}", {len(lang_list)-3});')
                ]]]*/
                memcpy(data, "P\x08.enUSdeDEfrFResESptPTitITtrTRkoKRjaJParSAelGRukUAruRUbeBYkkKZ", 63);
                raw_hid_send(data, length);
                memset(data, 0, length);
                memcpy(data, "P\x08.bgBGplPLroROzhCNnlNLheILsvSEfiFInnNOdaDKhuHUcsCZ", 51);
                //[[[end]]]
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
                        memcpy(data, "P\x09.", 3);
                    } else {
                        uprintf("Invalid language index %u.\n", new_lang);
                        memcpy(data, "P\x09!", 3);
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
                    //     memset(data, 0, length);
                    //     memcpy(data, "P\x0a.", 3);
                    // } else {
                    //     memset(data, 0, length);
                    //     memcpy(data, "P\x0a!", 3);
                    // }
                }
                break;
            case 11: //overlays flags on
                {
                    uint8_t new_flags = data[HID_DATA_IDX];
                    local_state->overlay_flags = flag_on(local_state->overlay_flags, new_flags);
                    // Run the self-actions immediately so subsequent HID commands (e.g.
                    // a send_overlay_mapping right after a reset_overlay_usage) see the
                    // post-action state.
                    apply_overlay_action_flags(new_flags);
                    // Force-sync state to slave whenever the host changes a bit that the slave
                    // needs to act on immediately — either an action flag (which triggers a
                    // mirror action on slave) or a synced state flag like MIRROR_OVERLAYS
                    // (which changes how slave's upload bridge handlers interpret subsequent
                    // overlay data). Without the latter, the host could set MIRROR_OVERLAYS
                    // alone (no action bits) and slave wouldn't learn until housekeeping
                    // ran — too late for the next upload bridge transaction.
                    const bool needs_force_sync =
                        (new_flags & OVERLAY_ACTION_FLAGS) || (new_flags & OVERLAY_SYNCED_STATE_FLAGS);
                    if(needs_force_sync) {
                        send_to_bridge(USER_SYNC_POLY_DATA, (void *)local_state, sizeof(poly_sync_t), 10);
                        if(new_flags & OVERLAY_ACTION_FLAGS) {
                            // Clear action bits locally so housekeeping has nothing to do.
                            local_state->overlay_flags &= ~OVERLAY_ACTION_FLAGS;
                        }
                        // Trigger refresh — for action-flag path the deferred handler used
                        // to do this at the end of its state_diff branch.
                        request_disp_refresh();
                    }
                    memset(data, 0, length);
                    memcpy(data, "P\x0b.", 3);
                    uprintf("Overlay flags 0x%x set.\n", new_flags);
                    raw_hid_send(data, length);
                }
                break;

            case 12: //overlays flags off
                {
                    uint8_t flags_to_clear = data[HID_DATA_IDX];
                    local_state->overlay_flags = flag_off(local_state->overlay_flags, flags_to_clear);
                    // Same reasoning as case 11: if a synced-state bit is being changed,
                    // tell the slave NOW so it doesn't act on stale state for any
                    // bridge transactions that follow.
                    if(flags_to_clear & OVERLAY_SYNCED_STATE_FLAGS) {
                        send_to_bridge(USER_SYNC_POLY_DATA, (void *)local_state, sizeof(poly_sync_t), 10);
                        request_disp_refresh();
                    }
                    memset(data, 0, length);
                    memcpy(data, "P\x0c.", 3);
                    uprintf("Overlay flags 0x%x cleared.\n", data[HID_DATA_IDX]);
                    raw_hid_send(data, length);
                }
                break;
            case 13: //set brightness
                if ( data[HID_DATA_IDX] <= FULL_BRIGHT) {
                    local_state->contrast = data[HID_DATA_IDX];
                    save_user_settings();
                    memset(data, 0, length);
                    memcpy(data, "P\x0d.", 3);
                    uprintf("Set brightness to: %u.\n", local_state->contrast);
                } else {
                    uprintf("Refused to set brightness to: %u.\n", data[HID_DATA_IDX]);
                    memset(data, 0, length);
                    memcpy(data, "P\x0d!", 3);
                }
                raw_hid_send(data, length);
                break;
            case 14: //key press
                {
                    uint16_t keycode = ((uint16_t)data[HID_DATA_IDX])<<8 | data[HID_DATA_IDX+1];
                    uint8_t r = 0, c = 0;
                    enum key_split_pos pos = get_split_matrix_pos(keycode, get_highest_layer(local_layer->layer), &r, &c, is_left_side());
                    const bool pressed = data[HID_DATA_IDX+2] == 0;
                    if(pos==POS_NOT_FOUND) {
                        //actually it should be the previous layer instead of default, but it worked so far
                        pos = get_split_matrix_pos(keycode, local_layer->def_layer, &r, &c, is_left_side());
                    }
                    if (is_on_current_side(pos)) {
                        invert_display(r, c, pressed);
                    }

                    //a key can be on both sides, so no else here
                    if (is_on_other_side(pos)) {
                        // send to bridge
                        const uint8_t data_len = 6;
                        dynamic_keymap_sync_t sync_data;
                        memcpy(&sync_data.commands, data, data_len);
                        send_to_bridge(USER_SYNC_DYNAMIC_KEYMAP_DATA, (void*)&sync_data, sizeof(sync_data.crc32)+data_len, 3);
                    }

                    action_exec(MAKE_KEYEVENT(r, c, pressed));
                    switch_events_poly(r,c, pressed);
                    memset(data, 0, length);
                    memcpy(data, "P\x0e.", 3);
                }
                raw_hid_send(data, length);
                break;
            case 15: //start/stop idle
                if(data[HID_DATA_IDX]==0) {
                    if((local_state->flags & (STATUS_DISP_ON|DISP_IDLE))==0) {
                        suspend_wakeup_init_kb();
                    } else {
                        local_state->flags &= ~((uint8_t)DISP_IDLE);
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
                memcpy(data, "P\x0f.", 3);
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
                    //     memset(data, 0, length);
                    //     memcpy(data, first ? "P\x10!" : "P\x11!", 3);
                    // } else {
                    //     memset(data, 0, length);
                    //     memcpy(data, first ? "P\x10!" : "P\x11!", 3);
                    // }
                }
                break;
            case 18: //start roi overlay
                set_fragment_context_from_buffer(&data[HID_DATA_IDX]);
                //fall through
            case 19: //receive roi overlay
                {
                    bool first = data[HID_CMD_IDX]==18;
                    if(get_fragment_context()->keycode>=KC_A && get_fragment_context()->keycode<=KC_RIGHT_GUI) {
                        fill_roi_overlay_buffer(&data[HID_DATA_IDX], first);
                    }
                    //     memset(data, 0, length);
                    //     memcpy(data, first ? "P\x12!" : "P\x13!", 3);
                    // } else {
                    //     memset(data, 0, length);
                    //     memcpy(data, first ? "P\x12!" : "P\x13!", 3);
                    // }
                }
                break;
            case 20: //set unicode input mode
                switch(data[HID_DATA_IDX]) {
                    case 0: //Linux = 0
                        unicode_input_mode_set_user(UNICODE_MODE_LINUX);
                        memset(data, 0, length);
                        memcpy(data, "P\x14.", 3);
                        break;
                    case 1: //Mac = 1
                        unicode_input_mode_set_user(UNICODE_MODE_MACOS);
                        memset(data, 0, length);
                        memcpy(data, "P\x14.", 3);
                        break;
                    case 2: //Windows = 2
                        unicode_input_mode_set_user(UNICODE_MODE_WINDOWS);
                        memset(data, 0, length);
                        memcpy(data, "P\x14.", 3);
                        break;
                    case 3: //WinCompose = 3
                        unicode_input_mode_set_user(UNICODE_MODE_WINCOMPOSE);
                        memset(data, 0, length);
                        memcpy(data, "P\x14.", 3);
                        break;
                    case 4: //BSD = 4
                        unicode_input_mode_set_user(UNICODE_MODE_BSD);
                        memset(data, 0, length);
                        memcpy(data, "P\x14.", 3);
                        break;
                    default:
                        memset(data, 0, length);
                        memcpy(data, "P\x14!", 3);
                        break;
                }
                raw_hid_send(data, length);
                break;
            case 21:
                {
                    overlay_map_sync_t map_sync;
                    memcpy(map_sync.mapping, &data[HID_DATA_IDX], HID_DATA_MAX);
                    send_to_bridge(USER_SYNC_OVERLAY_MAP_DATA, (void*)&map_sync, sizeof(overlay_map_sync_t), 10);
                    set_10bit_overlay_mapping(&data[HID_DATA_IDX]);
                    memset(data, 0, length);
                    memcpy(data, "P\x15.", 3);
                    uprintf("Overlay mapping data received.\n");
                    raw_hid_send(data, length);
                }
                break;
            case 22: // get default layer
                memset(data, 0, length);
                memcpy(data, "P\x16.", 3);
                data[3] = (uint8_t)local_layer->def_layer;
                raw_hid_send(data, length);
                break;
            default:
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
