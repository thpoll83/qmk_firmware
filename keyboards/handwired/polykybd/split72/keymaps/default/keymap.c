#include QMK_KEYBOARD_H

#include "quantum/quantum_keycodes.h"
#include "quantum/keymap_extras/keymap_german.h"
#include "quantum/keymap_introspection.h"
#include "quantum/via.h"

#include "raw_hid.h"
#include "oled_driver.h"
#include "version.h"
#include "print.h"
#include "debug.h"

#include <transactions.h>
#include <hardware/flash.h>

#include "polykybd.h"
#include "split72/split72.h"
#include "split72/sync_helper.h"
#include "split72/keymaps/default/uni.h"

#include "base/com.h"
#include "base/disp_array.h"
#include "base/helpers.h"
#include "base/spi_helper.h"
#include "base/shift_reg.h"
#include "base/text_helper.h"
#include "base/fonts/NotoSans_Regular_Base_11pt.h"
#include "base/fonts/NotoSans_Medium_Base_8pt.h"
#include "base/fonts/gfx_used_fonts.h"

#include "base/crc32.h"

#include "lang/lang_lut.h"
#include "lang/lang_lut_ext.h"

#include "layers.h"
#include "keycodes.h"
#include "uni.h"

//not used at the moment
#define FLASH_TARGET_OFFSET (4 * 1024 * 1024) //we start at 4MB and use the remaining 4MB for resource data
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
static_assert(FLASH_PAGE_SIZE==256, "Flash page size changed");

static enum lang_layer g_lang_init = INIT_LANG;

const struct display_info disp_row_0 = { BITMASK1(0) };
const struct display_info disp_row_3 = { BITMASK4(0) };

enum refresh_mode { START_FIRST_HALF, START_SECOND_HALF, DONE_ALL, ALL_AT_ONCE };
static enum refresh_mode g_refresh = DONE_ALL;

static poly_layer_t l_layer;
static poly_layer_t g_layer;

static poly_sync_t l_state;
static poly_sync_t g_state;

static poly_last_t l_last;
static poly_last_t g_last;

static latin_sync_t g_latin;

static int32_t last_update = 0;

static uint8_t use_overlay[NUM_OVERLAYS*NUM_VARIATIONS];
static uint8_t overlays [NUM_OVERLAYS*NUM_VARIATIONS][72*40/8]; // ResX*ResY/PixelPerByte

void reset_overlay_buffers(void) {
    memset(&use_overlay, 0, sizeof(use_overlay));
    memset(&overlays, 0, sizeof(overlays));
}

typedef struct _overlay_sync_t {
    uint32_t crc32;
    uint16_t adj_idx;
    uint8_t segment;
    uint8_t overlay[BYTES_PER_SEGMENT];
} overlay_sync_t;


bool display_wakeup(keyrecord_t* record);
void update_displays(enum refresh_mode mode);
void set_displays(uint8_t contrast, bool idle);
void set_selected_displays(int8_t old_value, int8_t new_value);
void toggle_stagger(bool new_state);
void oled_update_buffer(void);
void poly_suspend(void);

void save_user_eeconf(void) {
    poly_eeconf_t ee;
    ee.lang = l_state.lang;
    ee.brightness = ~l_state.contrast;
    ee.unused = 0;
    memcpy(ee.latin_ex, g_latin.ex, sizeof(g_latin.ex));
    eeconfig_update_user_datablock(&ee);
}

poly_eeconf_t load_user_eeconf(void) {
    poly_eeconf_t ee;
    eeconfig_read_user_datablock(&ee);
    ee.brightness = ~ee.brightness;
    if(ee.brightness>FULL_BRIGHT) {
        ee.brightness = FULL_BRIGHT;
    }
    return ee;
}

void inc_brightness(void) {
    if (l_state.contrast < FULL_BRIGHT) {
        l_state.contrast += BRIGHT_STEP;
    }
    if (l_state.contrast > FULL_BRIGHT) {
        l_state.contrast = FULL_BRIGHT;
    }

    save_user_eeconf();
}

void dec_brightness(void) {
    if (l_state.contrast > (MIN_BRIGHT+BRIGHT_STEP)) {
        l_state.contrast -= BRIGHT_STEP;
    } else {
        l_state.contrast = MIN_BRIGHT;
    }

    save_user_eeconf();
}

void select_all_displays(void) {
    // make sure we are talking to all shift registers
    sr_shift_out_0_latch(NUM_SHIFT_REGISTERS);
}

void clear_all_displays(void) {
    select_all_displays();

    kdisp_set_buffer(0x00);
    kdisp_send_buffer();
}

void early_hardware_init_post(void) {
    spi_hw_setup();
}

void update_performed(void) {
    last_update = timer_read32();
}

layer_state_t persistent_default_layer_get(void) {
    return (layer_state_t)eeconfig_read_default_layer();
}

void persistent_default_layer_set(uint16_t default_layer) {
  eeconfig_update_default_layer(default_layer);
  default_layer_set(default_layer);
}

void request_disp_refresh(void) {
    g_refresh = ALL_AT_ONCE;
    //use the following for partial update (during housekeeping)
    // g_refresh = START_FIRST_HALF;
}

//
// __/\\\\\\\\\\\\______________________________________________________________/\\\\\\\\\\\_____________________________________________
//  _\/\\\////////\\\__________________________________________________________/\\\/////////\\\___________________________________________
//   _\/\\\______\//\\\____________________/\\\________________________________\//\\\______\///_____/\\\__/\\\_____________________________
//    _\/\\\_______\/\\\__/\\\\\\\\\_____/\\\\\\\\\\\__/\\\\\\\\\________________\////\\\___________\//\\\/\\\___/\\/\\\\\\_______/\\\\\\\\_
//     _\/\\\_______\/\\\_\////////\\\___\////\\\////__\////////\\\__________________\////\\\_________\//\\\\\___\/\\\////\\\____/\\\//////__
//      _\/\\\_______\/\\\___/\\\\\\\\\\_____\/\\\________/\\\\\\\\\\____________________\////\\\_______\//\\\____\/\\\__\//\\\__/\\\_________
//       _\/\\\_______/\\\___/\\\/////\\\_____\/\\\_/\\___/\\\/////\\\_____________/\\\______\//\\\___/\\_/\\\_____\/\\\___\/\\\_\//\\\________
//        _\/\\\\\\\\\\\\/___\//\\\\\\\\/\\____\//\\\\\___\//\\\\\\\\/\\___________\///\\\\\\\\\\\/___\//\\\\/______\/\\\___\/\\\__\///\\\\\\\\_
//         _\////////////______\////////\//______\/////_____\////////\//______________\///////////______\////________\///____\///_____\////////__
//

void user_sync_poly_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(poly_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        if(crc32 == ((const poly_sync_t *)in_data)->crc32) {
            memcpy(&l_state, in_data, sizeof(poly_sync_t));
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
            //request_disp_refresh();
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

void user_sync_latin_ex_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(latin_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        if(crc32 == ((const latin_sync_t *)in_data)->crc32) {
            memcpy(&g_latin, in_data, sizeof(latin_sync_t));
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
            save_user_eeconf();
            request_disp_refresh();
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

#ifdef VIA_ENABLE

#ifndef DYNAMIC_KEYMAP_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_ADDR VIA_EEPROM_CONFIG_END
#endif
#define DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT 9
_Static_assert(DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT <= DYNAMIC_KEYMAP_LAYER_COUNT, "Maximum cannot exceed DYNAMIC_KEYMAP_LAYER_COUNT");

void dynamic_keymap_set_buffer_poly(uint16_t offset, uint16_t size, const uint8_t *data) {
    uint16_t dynamic_keymap_eeprom_size = DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2;
    void *   target                     = (void *)(DYNAMIC_KEYMAP_EEPROM_ADDR + offset);
    const uint8_t *source                     = data;
    for (uint16_t i = 0; i < size; i++) {
        if (offset + i < dynamic_keymap_eeprom_size) {
            eeprom_update_byte(target, *source);
        }
        source++;
        target++;
    }
}

void user_sync_via_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len >= (sizeof(uint32_t)+1) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        const via_sync_t* via_data = (const via_sync_t *)in_data;
        if(crc32 == via_data->crc32) {
            const uint8_t* command_data = &via_data->via_commands[1];
            switch(via_data->via_commands[0]) {
                case id_dynamic_keymap_reset:
                    dynamic_keymap_reset();
                    request_disp_refresh();
                    break;
                case id_dynamic_keymap_set_keycode:
                    dynamic_keymap_set_keycode(command_data[0], command_data[1], command_data[2], (command_data[3] << 8) | command_data[4]);
                    request_disp_refresh();
                    break;
                case id_dynamic_keymap_set_buffer: {
                    uint16_t offset = (command_data[0] << 8) | command_data[1];
                    uint16_t size   = command_data[2]; // size <= 28
                    dynamic_keymap_set_buffer_poly(offset, size, &command_data[3]);
                    request_disp_refresh();
                    break;
                }
                case id_custom_save:
                case 'P':
                    if(command_data[0]==14) {
                        uint16_t keycode = ((uint16_t)command_data[2])<<8 | command_data[3];
                        uint8_t r, c;
                        enum key_split_pos pos = get_split_matrix_pos(keycode, get_highest_layer(l_layer.layer), &r, &c, is_left_side());
                        if(pos==POS_NOT_FOUND) {
                            //actually it should be the previous layer instead of default, but it worked so far
                            pos = get_split_matrix_pos(keycode, l_layer.def_layer, &r, &c, is_left_side());
                        }
                        if (is_on_current_side(pos)) {
                            invert_display(r, c, command_data[4] == 0);
                        }
                    }
                default: break;
            }
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}
#endif

void user_sync_lastkey_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(poly_last_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        if(crc32 == ((const poly_last_t *)in_data)->crc32) {
            memcpy(&l_last, in_data, sizeof(poly_last_t));
            memcpy(&g_last, in_data, sizeof(poly_last_t));
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
            request_disp_refresh();
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

void user_sync_layer_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(poly_layer_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        if(crc32 == ((const poly_layer_t *)in_data)->crc32) {
            memcpy(&l_layer, in_data, sizeof(poly_layer_t));
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
            //request_disp_refresh();
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

void user_sync_overlay_data_handler(uint8_t in_len, const void* in_data, uint8_t out_len, void* out_data) {
    if (in_len == sizeof(overlay_sync_t) && in_data != NULL && out_len == sizeof(poly_sync_reply_t) && out_data!= NULL) {
        uint32_t crc32 = crc32_1byte(&((uint8_t *)in_data)[4], in_len-4, 0);
        const overlay_sync_t* ov = ((const overlay_sync_t *)in_data);
        if(crc32 == ov->crc32) {
            memcpy(overlays[ov->adj_idx] + ov->segment*BYTES_PER_SEGMENT, ov->overlay, BYTES_PER_SEGMENT);
            if(ov->segment==NUM_SEGMENTS_PER_OVERLAY-1) {
                use_overlay[ov->adj_idx] = true;
                request_disp_refresh();
            }
            ((poly_sync_reply_t*)out_data)->ack = SYNC_ACK;
        } else {
            ((poly_sync_reply_t*)out_data)->ack = SYNC_CRC32_ERR;
        }
    }
}

//void oled_on_off(bool on);

#define BYTE_TO_BINARY_PATTERN "|%s%s%s%s%s%s%s%s"
#define BYTE_TO_FLAGS(byte)  \
  ((byte) & 0x80 ? " RGB |" : " --- |"), \
  ((byte) & 0x40 ? "Txt2 |" : " --- |"), \
  ((byte) & 0x20 ? "Txt1 |" : " --- |"), \
  ((byte) & 0x10 ? " Dbg |" : " --- |"), \
  ((byte) & 0x08 ? "DeadK|" : " --- |"), \
  ((byte) & 0x04 ? "Idle |" : " --- |"), \
  ((byte) & 0x02 ? "Trans|" : " --- |"), \
  ((byte) & 0x01 ? "StatD|" : " --- |")

  #define BYTE_TO_OVERLAY_FLAGS(byte)  \
  ((byte) & 0x80 ? "  1  |" : " --- |"), \
  ((byte) & 0x40 ? "  1  |" : " --- |"), \
  ((byte) & 0x20 ? "Reset|" : " --- |"), \
  ((byte) & 0x10 ? "ClrRB|" : " --- |"), \
  ((byte) & 0x08 ? "ClrRT|" : " --- |"), \
  ((byte) & 0x04 ? "ClrLB|" : " --- |"), \
  ((byte) & 0x02 ? "ClrLT|" : " --- |"), \
  ((byte) & 0x01 ? "Disp |" : " --- |")

//helpers
static uint8_t flags = 0;
static uint8_t overlay_flags = 0;

void sync_and_refresh_displays(void) {
    bool layer_diff = false;
    bool state_diff = false;

    if (is_usb_host_side()) {
        const bool back_from_idle_transition = flag_turned_on(l_state.flags, g_state.flags, IDLE_TRANSITION);
        if (back_from_idle_transition) {
            poly_eeconf_t ee   = load_user_eeconf();
            l_state.contrast = ee.brightness;
        }

        if(flags!=l_state.flags) {
            uprintf("Poly State Flags: 0x%02x " BYTE_TO_BINARY_PATTERN "\n", l_state.flags, BYTE_TO_FLAGS(l_state.flags));
            flags=l_state.flags;
        }
        if(overlay_flags!=l_state.overlay_flags) {
            uprintf("Poly Ovrly Flags: 0x%02x " BYTE_TO_BINARY_PATTERN "\n", l_state.overlay_flags, BYTE_TO_OVERLAY_FLAGS(l_state.overlay_flags));
            overlay_flags=l_state.overlay_flags;
        }

        l_state.flags = set_flag(l_state.flags, DBG_ON, debug_enable);
        state_diff = differ(&l_state, &g_state, sizeof(poly_sync_t));
        if ( state_diff ) {
            send_to_bridge(USER_SYNC_POLY_DATA, (void *)&l_state, sizeof(l_state), 10);
        }

        l_layer.led_state = host_keyboard_led_state();
        l_layer.mods = get_mods();
        layer_diff = differ(&l_layer, &g_layer, sizeof(poly_layer_t));
        if ( layer_diff ) {
            send_to_bridge(USER_SYNC_LAYER_DATA, (void *)&l_layer, sizeof(l_layer), 10);
        }
        if ( differ(&l_last, &g_last, sizeof(poly_last_t)) ) {
             send_to_bridge(USER_SYNC_LASTKEY_DATA, (void *)&l_last, sizeof(l_last), 5);
             memcpy(&g_last, &l_last, sizeof(l_last));
        }
    } else {
        layer_diff = differ(&l_layer, &g_layer, sizeof(poly_layer_t));
        state_diff = differ(&l_state, &g_state, sizeof(poly_sync_t));
    }

    const bool in_idle_mode = (l_state.flags & DISP_IDLE) != 0;

    if(state_diff) {
        const bool idle_changed         = has_flag_changed(l_state.flags, g_state.flags, DISP_IDLE);
        const bool contrast_changed     = g_state.contrast != l_state.contrast;
        const bool status_disp_changed  = has_flag_changed(l_state.flags, g_state.flags, STATUS_DISP_ON);
        const bool status_disp_on       = test_flag(l_state.flags, STATUS_DISP_ON);
        const bool overlays_changed     = has_flag_changed(l_state.overlay_flags, g_state.overlay_flags, DISPLAY_OVERLAYS);
        const bool reset_overlays       = test_flag(l_state.overlay_flags, RESET_BUFFERS);
        const bool debug_changed        = has_flag_changed(l_state.flags, g_state.flags, DBG_ON);

        if(idle_changed) {
            if(in_idle_mode) {
                oled_set_brightness(0);
            }
        }

        //bool restored = false;
        if(status_disp_changed && status_disp_on) {
            // rgb_matrix_reload_from_eeprom();
            // if(rgb_matrix_is_enabled()) {
            //     l_state.flags = flag_on(l_state.flags, RGB_ON);
            //     restored = true;
            // }
            oled_set_brightness(OLED_BRIGHTNESS);
        }

        if(has_flag_changed(l_state.flags, g_state.flags, RGB_ON)) {
            if (test_flag(l_state.flags, RGB_ON)) {
                // if(restored) {
                //     rgb_matrix_enable_noeeprom();
                // } else {
                    rgb_matrix_enable();
                //}
            } else {
                //  if(!status_disp_on) {
                //     rgb_matrix_disable_noeeprom();
                // } else {
                    rgb_matrix_disable();
                //}
            }
        }

        if(reset_overlays) {
            reset_overlay_buffers();
            l_state.overlay_flags = flag_off(l_state.overlay_flags, RESET_BUFFERS);
        }

        if( debug_changed || overlays_changed || reset_overlays || l_state.unicode_mode!=g_state.unicode_mode) {
            request_disp_refresh();
            update_performed();
        }

        if (contrast_changed || idle_changed) {
            set_displays(l_state.contrast, in_idle_mode);
        }

        memcpy(&g_state, &l_state, sizeof(l_state));
    }

    if(layer_diff) {
        memcpy(&g_layer, &l_layer, sizeof(l_layer));
        request_disp_refresh();
    }

    if (g_refresh == START_FIRST_HALF) {
        update_displays(START_FIRST_HALF);
        g_refresh = START_SECOND_HALF;
    }
    else if (g_refresh == START_SECOND_HALF || g_refresh == ALL_AT_ONCE) {
        update_displays(g_refresh);
        g_refresh = DONE_ALL;
    }
}

layer_state_t layer_state_set_user(layer_state_t state) {
    l_layer.layer = state;
    return state;
}

void housekeeping_task_user(void) {
    sync_and_refresh_displays();

    if(last_update>=0) {
        //turn off displays
        uint32_t elapsed_time_since_update = timer_elapsed32(last_update);

        if (is_usb_host_side()) {
            l_state.flags |= STATUS_DISP_ON;
            l_state.flags &= ~((uint8_t)IDLE_TRANSITION);

            if(elapsed_time_since_update > FADE_OUT_TIME && l_state.contrast >= MIN_BRIGHT && (l_state.flags & DISP_IDLE)==0) {
                poly_eeconf_t ee = load_user_eeconf();
                int32_t time_after = elapsed_time_since_update - FADE_OUT_TIME;
                int16_t brightness = ((FADE_TRANSITION_TIME - time_after) * ee.brightness) / FADE_TRANSITION_TIME;

                //transition to pulsing mode
                if(brightness<=MIN_BRIGHT) {
                    l_state.contrast = DISP_OFF;
                    l_state.flags |= DISP_IDLE;
                    uprint("Transition to pulsing\n");
                } else if(brightness>FULL_BRIGHT) {
                    l_state.contrast = FULL_BRIGHT;
                    uprint("Limiting brightness\n");
                } else{
                    l_state.contrast = brightness;
                }
                l_state.flags |= IDLE_TRANSITION;
            } else if(elapsed_time_since_update > TURN_OFF_TIME) {
                uprint("Turning off\n");
                poly_suspend();
                last_update=-1;
            } else if((l_state.flags & DISP_IDLE)!=0) {
                int32_t time_after = PK_MAX(elapsed_time_since_update - FADE_OUT_TIME - FADE_TRANSITION_TIME, 0)/300;
                l_state.contrast = time_after%50;
            } else {
                l_state.flags &= ~((uint8_t)DISP_IDLE);
            }
        }
    }
}

#define LAYOUT_left_right_stacked(\
    lc0r0, lc1r0 ,lc2r0, lc3r0, lc4r0, lc5r0 ,lc6r0, \
    lc0r1, lc1r1 ,lc2r1, lc3r1, lc4r1, lc5r1 ,lc6r1, \
    lc0r2, lc1r2 ,lc2r2, lc3r2, lc4r2, lc5r2 ,lc6r2, lc7r3, \
    lc0r3, lc1r3 ,lc2r3, lc3r3, lc4r3, lc5r3 ,lc6r3, lc7r4, \
    lc0r4, lc1r4 ,lc2r4, lc3r4,        lc4r4 ,lc5r4, lc6r4, \
    \
           rc1r0 ,rc2r0, rc3r0, rc4r0, rc5r0 ,rc6r0, rc7r0, \
           rc1r1 ,rc2r1, rc3r1, rc4r1, rc5r1 ,rc6r1, rc7r1, \
    rc0r3, rc1r2 ,rc2r2, rc3r2, rc4r2, rc5r2 ,rc6r2, rc7r2, \
    rc0r4, rc1r3 ,rc2r3, rc3r3, rc4r3, rc5r3 ,rc6r3, rc7r3, \
    rc1r4, rc2r4 ,rc3r4,        rc4r4, rc5r4 ,rc6r4, rc7r4 \
  ) { \
    { lc0r0, lc1r0 ,lc2r0, lc3r0, lc4r0, lc5r0 ,lc6r0, KC_NO }, \
    { lc0r1, lc1r1 ,lc2r1, lc3r1, lc4r1, lc5r1 ,lc6r1, KC_NO }, \
    { lc0r2, lc1r2 ,lc2r2, lc3r2, lc4r2, lc5r2 ,lc6r2, KC_NO }, \
    { lc0r3, lc1r3 ,lc2r3, lc3r3, lc4r3, lc5r3 ,lc6r3, lc7r3 }, \
    { lc0r4, lc1r4 ,lc2r4, lc3r4, lc4r4, lc5r4 ,lc6r4, lc7r4 }, \
    \
    { KC_NO, rc1r0 ,rc2r0, rc3r0, rc4r0, rc5r0 ,rc6r0, rc7r0 }, \
    { KC_NO, rc1r1 ,rc2r1, rc3r1, rc4r1, rc5r1 ,rc6r1, rc7r1 }, \
    { KC_NO, rc1r2 ,rc2r2, rc3r2, rc4r2, rc5r2 ,rc6r2, rc7r2 }, \
    { rc0r3, rc1r3 ,rc2r3, rc3r3, rc4r3, rc5r3 ,rc6r3, rc7r3 }, \
    { rc0r4, rc1r4 ,rc2r4, rc3r4, rc4r4, rc5r4 ,rc6r4, rc7r4 } \
}

const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    //Base Layers
/*
                                                              ┌────────────────┐
                                                              │     QWERTY     │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │  Nubs │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │ BckSp  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   e   │   r   │   t   │   `   ├─╯                ╰─┤  Hypr │   y   │   u   │   i   │   o   │   p   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   d   │   f   │   g   │   '   │  (MB1)             │  Intl │   h   │   j   │   k   │   l   │   =   │  Ret   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   c   │   v   │   b   │  Nuhs │  Num!  │  │   [    │   ]   │   n   │   m   │   ,   │   ;   │  Up   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Lang  │   /   │ Space  │      │   .   │  Left │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L0] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_NUBS,
        KC_TAB,     KC_Q,       KC_W,       KC_E,       KC_R,       KC_T,       KC_GRAVE,
        MO(_FL0),   KC_A,       KC_S,       KC_D,       KC_F,       KC_G,       KC_QUOTE,   KC_MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       TO(_EMJ0),   MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_BSPC,
                    KC_HYPR,    KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       KC_BSLS,
        KC_NO,      MO(_ADDLANG1),KC_H,     KC_J,       KC_K,       KC_L,       KC_EQUAL,   KC_ENTER,
        KC_LBRC,    KC_RBRC,    KC_N,       KC_M,       KC_COMMA,   KC_SCLN,    KC_UP,      KC_RSFT,
        KC_LANG,    KC_SLASH,    KC_SPC,                KC_DOT,     KC_LEFT,    KC_DOWN,    KC_RIGHT
        ),

/*
                                                              ┌────────────────┐
                                                              │     QWERTY!    │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   6   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   7   │   8   │   9   │   0   │   -   │   =   │  Hypr  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   e   │   r   │   t   │   `   ├─╯                ╰─┤   y   │   u   │   i   │   o   │   p   │   [   │  Nubs  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   d   │   f   │   g   │   '   │  (MB1)             │   h   │   j   │   k   │   l   │   ;   │   ]   │    \   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │ Nuhs  │   z   │   x   │   c   │   v   │   b   │  Num!  │  │  Lang  │  Ctx  │   n   │   m   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Intl │      │  Space │  Del  │   Ins  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/

    [_L1] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_6,
        KC_TAB,     KC_Q,       KC_W,       KC_E,       KC_R,       KC_T,       KC_GRAVE,
        MO(_FL1),   KC_A,       KC_S,       KC_D,       KC_F,       KC_G,       KC_QUOTE,   KC_MS_BTN1,
        KC_LSFT,    TO(_EMJ0),   KC_Z,       KC_X,       KC_C,       KC_V,       KC_B,       MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    MO(_ADDLANG1),          KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,   KC_HYPR,
                    KC_Y,       KC_U,       KC_I,       KC_O,       KC_P,       KC_LBRC,    KC_NUBS,
        KC_NO,      KC_H,       KC_J,       KC_K,       KC_L,       KC_SCLN,    KC_RBRC,    KC_BSLS,
        KC_LANG,    KC_APP,     KC_N,       KC_M,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
/*
                                                              ┌────────────────┐
                                                              │   Colemak DH   │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │  Nub  │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   =    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   w   │   f   │   p   │   b   │   `   ├─╯                ╰─┤   j   │   l   │   u   │   y   │   ;   │   [   │  Intl  │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   r   │   s   │   t   │   g   │   '   │  (MB1)             │   m   │   n   │   e   │   i   │   o   │   ]   │    \   │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   c   │   d   │   v   │  Nuhs |  Num!  │  │  Lang  │  Hypr │   k   │   h   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L2] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_NUBS,
        KC_TAB,     KC_Q,       KC_W,       KC_F,       KC_P,       KC_B,       KC_GRAVE,
        MO(_FL1),   KC_A,       KC_R,       KC_S,       KC_T,       KC_G,       KC_QUOTE,   KC_MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_C,       KC_D,       KC_V,       TO(_EMJ0),    MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,
                    KC_J,       KC_L,       KC_U,       KC_Y,       KC_SCLN,    KC_LBRC,    MO(_ADDLANG1),
        KC_NO,      KC_M,       KC_N,       KC_E,       KC_I,       KC_O,       KC_RBRC,    KC_BSLS,
        KC_LANG,    KC_HYPR,    KC_K,       KC_H,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
        /*
                                                              ┌────────────────┐
                                                              │       Neo      │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   <   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   `    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   x   │   v   │   l   │   c   │   w   │   ^   ├─╯                ╰─┤   k   │   h   │   g   │   f   │   q   │   ß   │   ´    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   u   │   i   │   a   │   e   │   o   │   '   │  (MB1)             │   s   │   n   │   r   │   t   │   d   │   y   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   #   │   ü   │   ö   │   ä   │   p   │   z   │  Num!  │  │  Lang  │   +   │   b   │   m   │   ,   │   .   │   j   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L3] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       DE_LABK,
        KC_TAB,     KC_X,       KC_V,       KC_L,       KC_C,       KC_W,       DE_CIRC,
        MO(_FL0),   KC_U,       KC_I,       KC_A,       KC_E,       KC_O,       KC_QUOTE,   KC_MS_BTN1,
        KC_LSFT,    DE_HASH,    DE_UDIA,    DE_ODIA,    DE_ADIA,    KC_P,       DE_Z,       MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       DE_MINS,    DE_GRV,
                    KC_K,       KC_H,       KC_G,       KC_F,       KC_Q,       DE_SS,      DE_ACUT,
        KC_NO,      KC_S,       KC_N,       KC_R,       KC_T,       KC_D,       DE_Y,       KC_BSLS,
        KC_LANG,    DE_PLUS,    KC_B,       KC_M,       KC_COMMA,   KC_DOT,     KC_J,       KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
        /*
                                                              ┌────────────────┐
                                                              │    Workman     │
                                                              └────────────────┘
   ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┐                    ┌───────┬───────┬───────┬───────┬───────┬───────┬────────┐
   │  Esc   │   1   │   2   │   3   │   4   │   5   │   `   │ ╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮ │   6   │   7   │   8   │   9   │   0   │   -   │   =    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤ │╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯│ ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  TAB   │   q   │   d   │   r   │   w   │   b   │  Hypr ├─╯                ╰─┤   j   │   f   │   u   │   p   │   ;   │   [   │   ]    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┤                    ├───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │  FN    │   a   │   s   │   h   │   t   │   g   │  Meh  │  (MB1)             │   y   │   n   │   e   │   o   │   i   │   '   │   \    │
   ├────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────╮  ╭────────┼───────┼───────┼───────┼───────┼───────┼───────┼────────┤
   │ Shift  │   z   │   x   │   m   │   c   │   v   │  Intl │  Num!  │  │  Lang  │   k   │   b   │   l   │   ,   │   .   │   /   │ Shift  │
   └┬───────┼───────┼───────┼───────┼──────┬┴───────┼───────┼────────┤  ├────────┼───────┼───────┴┬──────┼───────┼───────┼───────┼───────┬┘
    │ Ctrl  │  Os   │  Alt  │  Ctx  │      │  Space │  Del  │   Ret  │  │  Ret   │ BckSp │ Space  │      │ Left  │   Up  │  Down │ Right │
    └───────┴───────┴───────┴───────┘      └────────┴───────┴────────╯  └────────┴───────┴────────┘      └───────┴───────┴───────┴───────┘
*/
    [_L4] = LAYOUT_left_right_stacked(
        KC_ESC,     KC_1,       KC_2,       KC_3,       KC_4,       KC_5,       KC_GRAVE,
        KC_TAB,     KC_Q,       KC_D,       KC_R,       KC_W,       KC_B,       KC_HYPR,
        MO(_FL1),   KC_A,       KC_S,       KC_H,       KC_T,       KC_G,       TO(_EMJ0),     KC_MS_BTN1,
        KC_LSFT,    KC_Z,       KC_X,       KC_M,       KC_C,       KC_V,       MO(_ADDLANG1), MO(_NL),
        KC_LCTL,    KC_LWIN,    KC_LALT,    KC_APP,                 KC_SPACE,   KC_DEL,     KC_ENTER,

                    KC_6,       KC_7,       KC_8,       KC_9,       KC_0,       KC_MINUS,   KC_EQUAL,
                    KC_J,       KC_F,       KC_U,       KC_P,       KC_SCLN,    KC_LBRC,    KC_RBRC,
        KC_NO,      KC_Y,       KC_N,       KC_E,       KC_O,       KC_I,       KC_QUOTE,   KC_BSLS,
        KC_LANG,    KC_K,       KC_B,       KC_L,       KC_COMMA,   KC_DOT,     KC_SLASH,   KC_RSFT,
        KC_ENTER,   KC_BSPC,    KC_SPC,                 KC_LEFT,    KC_UP,      KC_DOWN,    KC_RIGHT
        ),
    //Function Layer (Fn)
    [_FL0] = LAYOUT_left_right_stacked(
        OSL(_UL),   KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,     TO(_UL),
        _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        KC_CAPS,    _______,    KC_UNDO,    KC_CUT,     KC_COPY,    KC_PASTE,   _______,    _______,
        _______,    _______,    _______,    KC_BTN2,                _______,    _______,    _______,

                    KC_F6,      KC_F7,      KC_F8,      KC_F9,      KC_F10,      KC_F11,    KC_F12,
                    KC_AGAIN,   KC_BTN2,    _______,    KC_SELECT,  _______,    _______,    TO(_SL),
        _______,    KC_BTN3,    KC_OPER,    KC_CRSEL,   KC_EXSEL,   _______,    _______,    _______,
        TO(_NL),    KC_RALT,    _______,    _______,    _______,    _______,    _______,    KC_INS,
        KC_BTN1,    KC_RWIN,    KC_RCTL,                KC_HOME,    KC_PGUP,    KC_PGDN,    KC_END
        ),
    [_FL1] = LAYOUT_left_right_stacked(
        OSL(_UL),   KC_F1,      KC_F2,      KC_F3,      KC_F4,      KC_F5,      KC_F6,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    KC_FIND,    _______,    _______,    _______,
        _______,    _______,    KC_UNDO,    KC_CUT,     KC_COPY,    KC_PASTE,   _______,    _______,
        _______,    _______,    _______,    _______,                _______,    _______,    KC_INS,

                    KC_F7,      KC_F8,      KC_F9,      KC_F10,     KC_F11,     KC_F12,     TO(_UL),
                    KC_AGAIN,   KC_BTN2,    _______,    KC_SELECT,  _______,    _______,    TO(_SL),
        _______,    KC_BTN3,    KC_OPER,    KC_CRSEL,   KC_EXSEL,   _______,    _______,    KC_CAPS,
        TO(_NL),    KC_RALT,    _______,    _______,    _______,    _______,    _______,    _______,
        KC_BTN1,    KC_RWIN,    KC_RCTL,                KC_HOME,    KC_PGUP,    KC_PGDN,    KC_END
        ),
     //Num Layer
    [_NL] = LAYOUT_left_right_stacked(
        KC_NO,      KC_NUM,     KC_PSLS,    KC_PAST,    KC_PMNS,    KC_NO,      KC_NO,
        KC_BTN1,    KC_KP_7,    KC_KP_8,    KC_KP_9,    KC_PPLS,    KC_INS,     KC_NO,
        KC_NO,      KC_KP_4,    KC_KP_5,    KC_KP_6,    KC_PPLS,    KC_DEL,     KC_NO,     _______,
        KC_NO,      KC_KP_1,    KC_KP_2,    KC_KP_3,    KC_PENT,    KC_NO,      KC_NO,     _______,
        KC_BASE,    KC_KP_0,    KC_PDOT,    KC_PENT,                KC_MS_BTN2, KC_NO,     KC_NO,

                    KC_NO,      KC_NO,      KC_NUM,     KC_PSLS,    KC_PAST,    KC_PMNS,   KC_NO,
                    KC_NO,      KC_INS,     KC_KP_7,    KC_KP_8,    KC_KP_9,    KC_PPLS,   KC_NO,
        _______,    KC_NO,      KC_DEL,     KC_KP_4,    KC_KP_5,    KC_KP_6,    KC_PPLS,   KC_NO,
        _______,    _______,    KC_NO,      KC_KP_1,    KC_KP_2,    KC_KP_3,    KC_PENT,   KC_NO,
        _______,    KC_NO,      KC_NO,                  KC_KP_0,    KC_PDOT,    KC_PENT,   KC_BASE
        ),
    //Util Layer
    [_UL] = LAYOUT_left_right_stacked(
        KC_NO,      KC_F13,     KC_F14,     KC_F15,     KC_F16,     KC_F17,     KC_F18,
        KC_MYCM,    KC_CALC,    KC_PSCR,    KC_SCRL,    KC_BRK,     KC_NO,      KC_NO,
        KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      _______,
        KC_LSFT,    KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        KC_BASE,    KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,

                    KC_F19,     KC_F20,     KC_F21,     KC_F22,     KC_F23,     KC_F24,     KC_NO,
                    KC_NO,      KC_MPRV,    KC_MPLY,    KC_MSTP,    KC_MNXT,    KC_NO,      TO(_SL),
        _______,    KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        KC_NO,      KC_NO,      KC_MUTE,    KC_VOLD,    KC_VOLU,    KC_NO,      KC_NO,      KC_RSFT,
        KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,      KC_BASE
        ),
    //Settings Layer
    [_SL] = LAYOUT_left_right_stacked(
        KC_DDIM,    KC_DMIN,    KC_D1Q,     KC_DHLF,    KC_D3Q,     KC_DMAX,    KC_DBRI,
        KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        KC_NO,      KC_L0,      KC_L1,      KC_L2,      KC_L3,      KC_L4,      KC_NO,      _______,
        KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      QK_RBT,
        KC_BASE,    LBL_TEXT,   KC_TOGMODS, KC_TOGTEXT,             KC_NO,      QK_MAKE,    QK_BOOT,


        //             RM_PREV,    RGB_M_SW,   RGB_M_R,    KC_RGB_TOG, RGB_M_P,    RGB_M_B,    RM_NEXT,
        //             KC_NO,      RM_SPDD,    RM_SPDU,    KC_NO,      RM_HUED,    RM_HUEU,    KC_NO,
        // _______,    KC_NO,      RM_VALD,    RM_VALU,    KC_NO,      RM_SATD,    RM_SATU,    KC_NO,
                    RGB_RMOD,   RGB_M_SW,   RGB_M_R,    KC_RGB_TOG, RGB_M_P,    RGB_M_B,    RGB_MOD,
                    KC_NO,      RGB_SPD,    RGB_SPI,    KC_NO,      RGB_HUD,    RGB_HUI,    KC_NO,
        _______,    KC_NO,      RGB_VAD,    RGB_VAI,    KC_NO,      RGB_SAD,    RGB_SAI,    KC_NO,
        EE_CLR,     KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
        DB_TOGG,    KC_DEADKEY, KC_NO,                  KC_NO,      KC_NO,      KC_NO,      KC_BASE
        ),

    //Language Selection Layer
    [_LL] = LAYOUT_left_right_stacked(
                        KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,
                        KC_NO,      KC_LANG_PT, KC_LANG_ES, KC_LANG_AR, KC_LANG_GR, KC_NO,      KC_NO,
        QK_UNICODE_MODE_WINCOMPOSE, KC_LANG_FR, KC_LANG_DE, KC_LANG_JA, KC_LANG_TR, KC_NO,      KC_NO,      KC_MS_BTN1,
        QK_UNICODE_MODE_EMACS,      KC_LANG_IT, KC_LANG_EN, KC_LANG_KO, KC_LANG_SV, KC_NO,      KC_NO,      KC_NO,
        KC_BASE,                    KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,

                    KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      KC_NO,      QK_UNICODE_MODE_MACOS,
                    KC_NO,      KC_LANG_UA, KC_LANG_BG, KC_LANG_BE, KC_LANG_RU, KC_NO,      QK_UNICODE_MODE_LINUX,
        _______,    KC_NO,      KC_LANG_KZ, KC_LANG_PL, KC_LANG_RO, KC_LANG_ZH, KC_NO,      QK_UNICODE_MODE_WINDOWS,
        KC_NO,      KC_NO,      KC_LANG_NL, KC_LANG_HE, KC_LANG_NO, KC_LANG_FI, KC_NO,      QK_UNICODE_MODE_BSD,
        KC_NO,      KC_NO,      KC_NO,                  KC_NO,      KC_NO,      KC_NO,      KC_BASE
        ),
    [_ADDLANG1] = LAYOUT_left_right_stacked(
        KC_NO,      KC_NO,      KC_LAT0,    KC_LAT1,    KC_LAT2,    KC_LAT3,    KC_LAT4,
        KC_NO,      _______,    _______,    _______,    _______,    _______,    _______,
        KC_NO,      _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        KC_NO,      KC_NO,      _______,    _______,                _______,    _______,    _______,

                    KC_LAT5,    KC_LAT6,    KC_LAT7,    KC_LAT8,    KC_LAT9,    KC_NO,      KC_NO,
                    _______,    _______,    _______,    _______,    _______,    _______,    KC_NO,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    KC_NO,
        _______,    _______,    _______,    _______,    _______,    _______,    _______,    _______,
        _______,    _______,    _______,                _______,    _______,    _______,    _______
        ),
    [_EMJ0] = LAYOUT_left_right_stacked(
        EMJ(0),    EMJ(1),    EMJ(2),    EMJ(3),    EMJ(4),    EMJ(5),    EMJ(6),
        EMJ(14),   EMJ(15),   EMJ(16),   EMJ(17),   EMJ(18),   EMJ(19),   EMJ(20),
        EMJ(28),   EMJ(29),   EMJ(30),   EMJ(31),   EMJ(32),   EMJ(33),   EMJ(34),   _______,
        EMJ(42),   EMJ(43),   EMJ(44),   EMJ(45),   EMJ(46),   EMJ(47),   EMJ(48),   EMJ(49),
        KC_BASE,   EMJ(58),   EMJ(59),   EMJ(60),              EMJ(61),   EMJ(62),   EMJ(63),

                  EMJ(7),    EMJ(8),    EMJ(9),    EMJ(10),   EMJ(11),   EMJ(12),   EMJ(13),
                  EMJ(21),   EMJ(22),   EMJ(23),   EMJ(24),   EMJ(25),   EMJ(26),   EMJ(27),
        _______,  EMJ(35),   EMJ(36),   EMJ(37),   EMJ(38),   EMJ(39),   EMJ(40),   EMJ(41),
        EMJ(50),  EMJ(51),   EMJ(52),   EMJ(53),   EMJ(54),   EMJ(55),   EMJ(56),   EMJ(57),
        EMJ(64),  EMJ(65),   EMJ(66),              EMJ(67),   EMJ(68),   EMJ(69),   TO(_EMJ1)
        ),
    [_EMJ1] = LAYOUT_left_right_stacked(
        EMJ(70+0),    EMJ(70+1),    EMJ(70+2),    EMJ(70+3),    EMJ(70+4),    EMJ(70+5),    EMJ(70+6),
        EMJ(70+14),   EMJ(70+15),   EMJ(70+16),   EMJ(70+17),   EMJ(70+18),   EMJ(70+19),   EMJ(70+20),
        EMJ(70+28),   EMJ(70+29),   EMJ(70+30),   EMJ(70+31),   EMJ(70+32),   EMJ(70+33),   EMJ(70+34),   _______,
        EMJ(70+42),   EMJ(70+43),   EMJ(70+44),   EMJ(70+45),   EMJ(70+46),   EMJ(70+47),   EMJ(70+48),   EMJ(70+49),
        KC_BASE,      EMJ(70+58),   EMJ(70+59),   EMJ(70+60),                 EMJ(70+61),   EMJ(70+62),   EMJ(70+63),

                     EMJ(70+7),    EMJ(70+8),    EMJ(70+9),    EMJ(70+10),   EMJ(70+11),   EMJ(70+12),   EMJ(70+13),
                     EMJ(70+21),   EMJ(70+22),   EMJ(70+23),   EMJ(70+24),   EMJ(70+25),   EMJ(70+26),   EMJ(70+27),
        _______,     EMJ(70+35),   EMJ(70+36),   EMJ(70+37),   EMJ(70+38),   EMJ(70+39),   EMJ(70+40),   EMJ(70+41),
        EMJ(70+50),  EMJ(70+51),   EMJ(70+52),   EMJ(70+53),   EMJ(70+54),   EMJ(70+55),   EMJ(70+56),   EMJ(70+57),
        EMJ(70+64),  EMJ(70+65),   EMJ(70+66),                 EMJ(70+67),   EMJ(70+68),   EMJ(70+69),   TO(_EMJ0)
        )
};


#define LX(x,y) ((x)/2),y
led_config_t g_led_config = { {// Key Matrix to LED Index
                              {6, 5, 4, 3, 2, 1, 0, NO_LED},
                              {13, 12, 11, 10, 9, 8, 7, NO_LED},
                              {20, 19, 18, 17, 16, 15, 14, NO_LED},
                              {27, 26, 25, 24, 23, 22, 21, NO_LED},
                              {35, 34, 33, 32, 31, 30, 29, 28},

                              {NO_LED, 42, 41, 40, 39, 38, 37, 36},
                              {NO_LED, 49, 48, 47, 46, 45, 44, 43},
                              {NO_LED, 56, 55, 54, 53, 52, 51, 50},
                              {NO_LED, 63, 62, 61, 60, 59, 58, 57},
                              {71, 70, 69, 68, 67, 66, 65, 64}
                             },
                             {
                                // LED Index to Physical Position
                                                {LX(144, 9)},   {LX(129, 9)},   {LX(104, 5)},   {LX(79, 1)},    {LX(55, 5)},    {LX(30, 9)},    {LX(0, 9)},
                                                {LX(144, 33)},  {LX(129, 33)},  {LX(104, 19)},  {LX(79, 25)},   {LX(55, 29)},   {LX(30, 33)},   {LX(0, 33)},
                                                {LX(144, 58)},  {LX(129, 58)},  {LX(104, 54)},  {LX(79, 50)},   {LX(55, 54)},   {LX(30, 58)},   {LX(0, 58)},
                                                {LX(144, 83)},  {LX(129, 83)},  {LX(104, 79)},  {LX(79, 75)},   {LX(55, 79)},   {LX(30, 83)},   {LX(0, 83)},
                {LX(170, 99)},  {LX(170, 127)}, {LX(144, 118)}, {LX(129, 113)},                 {LX(79, 99)},   {LX(55, 103)},  {LX(30, 107)},  {LX(6, 107)},

                                                {LX(446, 9)},   {LX(415, 9)},   {LX(390, 5)},   {LX(365, 1)},   {LX(341, 5)},   {LX(316, 9)},   {LX(286, 9)},
                                                {LX(446, 33)},  {LX(415, 33)},  {LX(390, 19)},  {LX(365, 25)},  {LX(341, 29)},  {LX(316, 33)},  {LX(286, 33)},
                                                {LX(446, 58)},  {LX(415, 58)},  {LX(390, 54)},  {LX(365, 50)},  {LX(341, 54)},  {LX(316, 58)},  {LX(286, 58)},
                                                {LX(446, 83)},  {LX(415, 83)},  {LX(390, 79)},  {LX(365, 75)},  {LX(341, 79)},  {LX(316, 83)},  {LX(286, 83)},
                                                {LX(440, 107)}, {LX(415, 107)}, {LX(390, 103)}, {LX(365, 99)},                  {LX(324, 113)}, {LX(290, 118)}, {LX(264, 127)},  {LX(264, 99)}
                             },
                             {
                                 // LED Index to Flag
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4, 4,

                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4,
                                 4, 4, 4, 4, 4, 4, 4, 4
                             } };

const uint16_t* to_static_text(uint16_t keycode, led_t state) {

    const uint16_t* emoji = keycode_to_emoji(keycode);
    if(emoji!=NULL) {
        return emoji;
    }

    if(IS_QK_MOD_TAP(keycode)) {
        keycode = QK_MOD_TAP_GET_TAP_KEYCODE(keycode);
    }

    const uint16_t* text = keycode_to_static_text(keycode, state, l_state.flags);
    if(text!=NULL) {
        return text;
    }

    switch (keycode) {
        case QK_UNICODE_MODE_MACOS:         return l_state.unicode_mode == UNICODE_MODE_MACOS ? u"Mac\r\v" ICON_SWITCH_ON : u"Mac\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_LINUX:         return l_state.unicode_mode == UNICODE_MODE_LINUX ? u"Lnx\r\v" ICON_SWITCH_ON : u"Lnx\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_WINDOWS:       return l_state.unicode_mode == UNICODE_MODE_WINDOWS ? u"Win\r\v" ICON_SWITCH_ON : u"Win\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_BSD:           return l_state.unicode_mode == UNICODE_MODE_BSD ? u"BSD\r\v" ICON_SWITCH_ON : u"BSD\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_WINCOMPOSE:    return l_state.unicode_mode == UNICODE_MODE_WINCOMPOSE ? u"WinC\r\v" ICON_SWITCH_ON : u"WinC\r\v" ICON_SWITCH_OFF;
        case QK_UNICODE_MODE_EMACS:         return l_state.unicode_mode == UNICODE_MODE_EMACS ? u"Emcs\r\v" ICON_SWITCH_ON : u"Emcs\r\v" ICON_SWITCH_OFF;

        case KC_L0:                         return l_layer.def_layer == _L0 ? u"Qwty\r\v" ICON_SWITCH_ON : u"Qwty\r\v" ICON_SWITCH_OFF;
        case KC_L1:                         return l_layer.def_layer == _L1 ? u"Qwty!\r\v" ICON_SWITCH_ON : u"Qwty!\r\v" ICON_SWITCH_OFF;
        case KC_L2:                         return l_layer.def_layer == _L2 ? u"Clmk\r\v" ICON_SWITCH_ON : u"Clmk\r\v" ICON_SWITCH_OFF;
        case KC_L3:                         return l_layer.def_layer == _L3 ? u"Neo\r\v" ICON_SWITCH_ON : u"Neo\r\v" ICON_SWITCH_OFF;
        case KC_L4:                         return l_layer.def_layer == _L4 ? u"Wkm\r\v" ICON_SWITCH_ON : u"Wkm\r\v" ICON_SWITCH_OFF;

        //Language selection keycodes
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
            languages.append(lang_key)
            lang_index = lang_index + 1
            lang_key = sheet.cell(row = 1, column = 2 + lang_index*4).value

        for lang in languages:
            short = lang.split("_")[1]
            cog.outl(f'case KC_{lang}: return l_state.lang == {lang} ? u"[{short}]" : u" {short}";')
        ]]]*/
        case KC_LANG_EN: return l_state.lang == LANG_EN ? u"[EN]" : u" EN";
        case KC_LANG_DE: return l_state.lang == LANG_DE ? u"[DE]" : u" DE";
        case KC_LANG_FR: return l_state.lang == LANG_FR ? u"[FR]" : u" FR";
        case KC_LANG_ES: return l_state.lang == LANG_ES ? u"[ES]" : u" ES";
        case KC_LANG_PT: return l_state.lang == LANG_PT ? u"[PT]" : u" PT";
        case KC_LANG_IT: return l_state.lang == LANG_IT ? u"[IT]" : u" IT";
        case KC_LANG_TR: return l_state.lang == LANG_TR ? u"[TR]" : u" TR";
        case KC_LANG_KO: return l_state.lang == LANG_KO ? u"[KO]" : u" KO";
        case KC_LANG_JA: return l_state.lang == LANG_JA ? u"[JA]" : u" JA";
        case KC_LANG_AR: return l_state.lang == LANG_AR ? u"[AR]" : u" AR";
        case KC_LANG_GR: return l_state.lang == LANG_GR ? u"[GR]" : u" GR";
        case KC_LANG_UA: return l_state.lang == LANG_UA ? u"[UA]" : u" UA";
        case KC_LANG_RU: return l_state.lang == LANG_RU ? u"[RU]" : u" RU";
        case KC_LANG_BE: return l_state.lang == LANG_BE ? u"[BE]" : u" BE";
        case KC_LANG_KZ: return l_state.lang == LANG_KZ ? u"[KZ]" : u" KZ";
        case KC_LANG_BG: return l_state.lang == LANG_BG ? u"[BG]" : u" BG";
        case KC_LANG_PL: return l_state.lang == LANG_PL ? u"[PL]" : u" PL";
        case KC_LANG_RO: return l_state.lang == LANG_RO ? u"[RO]" : u" RO";
        case KC_LANG_ZH: return l_state.lang == LANG_ZH ? u"[ZH]" : u" ZH";
        case KC_LANG_NL: return l_state.lang == LANG_NL ? u"[NL]" : u" NL";
        case KC_LANG_HE: return l_state.lang == LANG_HE ? u"[HE]" : u" HE";
        case KC_LANG_SV: return l_state.lang == LANG_SV ? u"[SV]" : u" SV";
        case KC_LANG_FI: return l_state.lang == LANG_FI ? u"[FI]" : u" FI";
        case KC_LANG_NO: return l_state.lang == LANG_NO ? u"[NO]" : u" NO";
        //[[[end]]]
        default:
            return NULL;
    }
}

bool render_key(uint16_t keycode, led_t state, uint8_t mods) {
    const bool shift = ((l_layer.mods & MOD_MASK_SHIFT) != 0);
    const bool add_lang = get_highest_layer(l_layer.layer)==_ADDLANG1;
    const bool alt = ((l_layer.mods & MOD_MASK_ALT) != 0);
    const bool is_letter = keycode>=KC_A && keycode<=KC_Z;
    if(is_letter && add_lang) {
        //display the previously selected latin variation of the letter
        const uint8_t offset = (shift || state.caps_lock) ? 0 : 26;
        uint8_t variation = (shift || state.caps_lock) ? g_latin.ex[keycode-KC_A]>>4 : g_latin.ex[keycode-KC_A]&0xf;

        const uint16_t* def_variation = latin_ex_map[offset+keycode-KC_A][0];
        if(def_variation!=NULL) {
            kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28, 23, latin_ex_map[offset+keycode-KC_A][variation]);
            return true;
        }
        return false;
    }
    //variation selection on 0~9
    if(keycode>=KC_LAT0 && keycode<=KC_LAT9) {
        if(add_lang && alt && l_last.latin_kc!=0) {
            //show all available alternatives for selected latin letter
            const uint8_t offset = (shift || state.caps_lock) ? 0 : 26;
            kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28, 23, latin_ex_map[offset+l_last.latin_kc-KC_A][keycode-KC_LAT0]);
            return true;
        }
        return false;
    }

    if (mods & MOD_RALT) {
        const uint16_t* letter = translate_keycode_only_altgr(l_state.lang, keycode);
        if (letter != NULL) {
            const bool is_num = keycode>=KC_1 && keycode<=KC_0; // yes the first is 1 and the last is 0
            int8_t v_set;
            int8_t h_set;
            if(is_num){
                v_set = SETTING_NUM_VOFFSET;
                h_set = SETTING_NUM_HOFFSET;
            } else {
                v_set = SETTING_SYM_VOFFSET;
                h_set = SETTING_SYM_HOFFSET;
            }
            int8_t v_off = get_setting(v_set, l_state.lang, VAR_SMALL);
            int8_t v_off_alt = get_setting(v_set, l_state.lang, VAR_ALTGR);
            v_off = PK_MIN(v_off, v_off_alt);
            int8_t h_off = get_setting(h_set, l_state.lang, VAR_SMALL);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);
                return true;
            }
        }
    }

    //translate to current language
    const uint16_t* letter = translate_keycode(l_state.lang, keycode, shift, state.caps_lock);
    if (letter != NULL) {
        int8_t v_set;
        int8_t h_set;
        if(is_letter) {
            v_set = SETTING_LETTER_VOFFSET;
            h_set = SETTING_LETTER_HOFFSET;
        } else {
            const bool is_num = keycode>=KC_1 && keycode<=KC_0; // yes the first is 1 and the last is 0
            if(is_num){
                v_set = SETTING_NUM_VOFFSET;
                h_set = SETTING_NUM_HOFFSET;
            } else {
                v_set = SETTING_SYM_VOFFSET;
                h_set = SETTING_SYM_HOFFSET;
            }
        }
        int8_t v_off = get_setting(v_set, l_state.lang, VAR_SMALL);
        int8_t h_off = get_setting(h_set, l_state.lang, VAR_SMALL);

        kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);

        //preview capital letter?
        if(!shift && !state.caps_lock) {
            v_off = get_setting(v_set, l_state.lang, VAR_SHIFT);
            h_off = get_setting(h_set, l_state.lang, VAR_SHIFT);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                letter = translate_keycode_only_shift(l_state.lang, keycode);
                if (letter != NULL) {
                    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);
                }
            }
        }
        //preview alt representation
        letter = translate_keycode_only_altgr(l_state.lang, keycode);
        if (letter != NULL) {
            v_off = get_setting(v_set, l_state.lang, VAR_ALTGR);
            h_off = get_setting(h_set, l_state.lang, VAR_ALTGR);
            if(v_off!=HIDE_KEY && h_off!=HIDE_KEY) {
                kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28+h_off, 23+v_off, letter);
            }
        }
        return true;
    }
    return false;
}

const uint16_t* keycode_to_disp_overlay(uint16_t keycode, led_t state) {
    switch (keycode)
    {
        case KC_F2: return u"      " PRIVATE_NOTE;
        case KC_F5: return u"     " ARROWS_CIRCLE;
        default: break;
    }

    if( (l_layer.mods & MOD_MASK_CTRL) != 0) {
        switch(keycode) {
            case KC_A: return u"      " BOX_WITH_CHECK_MARK;
            case KC_C: return u"     " CLIPBOARD_COPY;
            case KC_D: return u"\t " PRIVATE_DELETE;
            case KC_F: return u"    " PRIVATE_FIND;
            case KC_X: return u"\t\b\b\b\b" CLIPBOARD_CUT;
            case KC_V: return u"     " CLIPBOARD_PASTE;
            case KC_S: return u"\t" PRIVATE_FLOPPY;
            case KC_O: return u"\t" FILE_OPEN;
            case KC_P: return u"\t\b\b" PRIVATE_PRINTER;
            case KC_Z: return u"      " ARROWS_UNDO;
            case KC_Y: return u"      " ARROWS_REDO;
            default: break;
        }
    } else if((l_layer.mods & MOD_MASK_GUI) != 0) {
        switch(keycode) {
            case KC_D:      return u"    " PRIVATE_PC;
            case KC_L:      return u"    " PRIVATE_LOCK;
            case KC_P:      return u"    " PRIVATE_SCREEN;
            case KC_UP:     return u"     " PRIVATE_MAXIMIZE;
            case KC_DOWN:   return u"     " PRIVATE_WINDOW;
            default: break;
        }
    }

    if(IS_QK_MOD_TAP(keycode)) {
        uint8_t mods = QK_MOD_TAP_GET_MODS(keycode);
        if((mods & MOD_MASK_CSAG) == MOD_MASK_CSAG) {
            return u"    " CURRENCY_SIGN ICON_SHIFT NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SAG) == MOD_MASK_SAG) {
            return u"    " ICON_SHIFT NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CAG) == MOD_MASK_CAG) {
            return u"    " CURRENCY_SIGN NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CSG) == MOD_MASK_CSG) {
            return u"    " CURRENCY_SIGN ICON_SHIFT KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CSA) == MOD_MASK_CSA) {
            return u"    " CURRENCY_SIGN ICON_SHIFT NOT_SIGN;
        } else if((mods & MOD_MASK_AG) == MOD_MASK_AG) {
            return u"    " NOT_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SG) == MOD_MASK_SG) {
            return u"    " ICON_SHIFT KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_SA) == MOD_MASK_SA) {
            return u"    " CURRENCY_SIGN NOT_SIGN;
        } else if((mods & MOD_MASK_CG) == MOD_MASK_CG) {
            return u"    " CURRENCY_SIGN KATAKANA_MIDDLE_DOT;
        } else if((mods & MOD_MASK_CA) == MOD_MASK_CA) {
            return u"    " CURRENCY_SIGN NOT_SIGN;
        } else if((mods & MOD_MASK_CS) == MOD_MASK_CS) {
            return u"    " CURRENCY_SIGN ICON_SHIFT;
        } else if(mods & MOD_MASK_CTRL) {
            return u"    " CURRENCY_SIGN;
        } else if(mods & MOD_MASK_ALT) {
            return u"    " NOT_SIGN;
        } else if (mods & MOD_MASK_SHIFT) {
            return u"    " ICON_SHIFT;
        } else {
            return u"   " KATAKANA_MIDDLE_DOT;
        }
    }

    return NULL;
}

// NO_MOD(0), CTRL(1), SHIFT(2), CTRL_SHIFT(3), ALT(4), CTRL_ALT(5),
// ALT_SHIFT(6), {Not supported: CTRL_ALT_SHIFT(7) and GUI_KEY(8)}
uint16_t adjust_overlay_idx_to_mod(uint16_t idx, uint8_t mods) {
    //no differencce between r&l mods:
    mods |= mods>>4;
    mods &= 0x7;
    // if(mods>7) {  //no CTRL_ALT_SHIFT and no combo with GUI, so 7 == GUI
    //     return idx + NUM_OVERLAYS*7;
    // }

    return mods == 7 ? idx : idx + NUM_OVERLAYS * mods;
}

bool copy_overlay_to_buffer(uint16_t keycode, uint8_t mods, bool combine) {
    if(keycode>KC_RGUI || (keycode>KC_NUM_LOCK && keycode<KC_NUBS) || (keycode>KC_APP && keycode<KC_LEFT_CTRL)) {
        return false;
    }
    uint16_t idx = (keycode>KC_APP) ? (keycode - KC_LEFT_CTRL + 82) : (keycode>KC_NUM_LOCK ? keycode - KC_NUBS + 80 : keycode - KC_A);
    if(idx>=90) {
        return false;
    }
    idx = adjust_overlay_idx_to_mod(idx, mods);
    if(!use_overlay[idx]) {
        return false;
    }
    if(combine) {
        combine_with_mask();
    }
    kdisp_clear_bitmap_courtyard(28, 0, overlays[idx], 72, 40);
    kdisp_draw_bitmap(28, 0, overlays[idx], 72, 40); //don't understnad why we start at offset 28... need to think about it
    return true;
}

void update_displays(enum refresh_mode mode) {
    if(l_state.contrast<=DISP_OFF || (l_state.flags&DISP_IDLE)!=0) {
        return;
    }

    //uint8_t layer = get_highest_layer(layer_state);

    const led_t state = l_layer.led_state;
    const uint8_t mods = l_layer.mods;
    const bool capital_case = ((mods & MOD_MASK_SHIFT) != 0) || state.caps_lock;
    const bool display_overlays = test_flag(l_state.overlay_flags, DISPLAY_OVERLAYS);
    //the left side has an offset of 0, the right side an offset of MATRIX_ROWS_PER_SIDE
    const uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    uint8_t start_row = 0;

    //select first display (and later on shift that 0 till the end)
    if (mode == START_SECOND_HALF) {
        sr_shift_out_buffer_latch(disp_row_3.bitmask, sizeof(struct display_info));
        start_row = 3;
    }
    else {
        sr_shift_out_buffer_latch(disp_row_0.bitmask, sizeof(struct display_info));
    }

    const uint8_t max_rows = mode == START_FIRST_HALF ? 3 : MATRIX_ROWS_PER_SIDE;

    const bool overlay_only = test_flag( l_state.overlay_flags, CLEAR_LEFT_TOP|CLEAR_LEFT_BOTTOM|CLEAR_RIGHT_TOP|CLEAR_RIGHT_BOTTOM);
    const bool combine = !overlay_only && (l_state.overlay_flags & (CLEAR_LEFT_TOP|CLEAR_LEFT_BOTTOM|CLEAR_RIGHT_TOP|CLEAR_RIGHT_BOTTOM))!=0;
    if(combine ) {
        prepare_mask_buffer(l_state.overlay_flags);
    }

    uint8_t skip = 0;
    for (uint8_t r = start_row; r < max_rows; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            uint8_t  disp_idx = LAYOUT_TO_INDEX(r, c);

            //since MATRIX_COLS==8 we don't need to shift multiple times at the end of the row
            //except there was a leading and missing physical key (KC_NO on base layer)
            uint16_t keycode = keymaps[_BL][r + offset][c];
            if (keycode == KC_NO) {
                skip++;
            }
            else {
                if (disp_idx != 255) {
                    uint8_t layer = get_highest_layer(l_layer.layer);
                    uint16_t highest_kc = keycode_at_keymap_location(layer,r + offset,c); //if we encounter a transparent key go down one layer (but only one!)
                    keycode = (highest_kc == KC_TRNS) ? keycode_at_keymap_location(get_highest_layer(l_layer.layer&~(1<<layer)),r + offset,c) : highest_kc;
                    kdisp_enable(true);
                    kdisp_set_contrast(l_state.contrast-1);
                    if(keycode!=KC_TRNS) {
                        const uint16_t* text = to_static_text(keycode, state);
                        kdisp_set_buffer(0x00);
                        if(!overlay_only) {
                            if(text==NULL) {
                                if(!render_key(keycode, state, mods) && (keycode&QK_UNICODEMAP_PAIR)==QK_UNICODEMAP_PAIR){
                                    uint16_t chr = capital_case ? QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode) : QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
                                    kdisp_write_gfx_char(ALL_FONTS, ALL_FONT_SIZE, 28, 23, unicode_map[chr], false);
                                }
                            } else {
                                kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 28, 23, text);
                            }
                        }
                        text = NULL;
                        if(display_overlays) {
                            if(!copy_overlay_to_buffer(keycode, mods, combine)) {
                                text = keycode_to_disp_overlay(keycode, state); //fallback to hardcoded
                            }
                        } else {
                            text = keycode_to_disp_overlay(keycode, state); //this should maybe go away - or setting?
                        }
                        if(text) {
                            kdisp_write_gfx_text_cy(ALL_FONTS, ALL_FONT_SIZE, 28, 23, text, true);
                        }
                        kdisp_send_buffer();
                    }
                }
                sr_shift_once_latch();
            }

        }
        for (;skip > 0;skip--) {
            sr_shift_once_latch();
        }
    }
}

uint8_t to_brightness(uint8_t b) {
    switch(b) {
        case 23: case 24: case 25: case 26: case 27: return 7;
        case 22: case 28: return 6;
        case 21: case 29: return 5;
        case 20: case 30: return 4;
        case 19: case 31: return 3;
        case 18: case 32: return 2;
        case 1: case 7: return 1;
        case 2: case 6: return 3;
        case 3: case 4: case 5: return 5;
        default: return 0;
    }
}

void kdisp_idle(uint8_t contrast) {
    uint8_t offset = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    uint8_t skip = 0;
    sr_shift_out_buffer_latch(disp_row_0.bitmask, sizeof(struct display_info));

    //uint8_t idx = 0;
    for (uint8_t r = 0; r < MATRIX_ROWS_PER_SIDE; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            uint8_t  disp_idx = LAYOUT_TO_INDEX(r, c);

            //since MATRIX_COLS==8 we don't need to shift multiple times at the end of the row
            //except there was a leading and missing physical key (KC_NO on base layer)
            uint16_t keycode = keymaps[_BL][r + offset][c];
            if (keycode == KC_NO) {
                skip++;
            } else {
                if (disp_idx != 255) {
                    uint8_t idle_brightness = to_brightness((contrast+(c%3+r)*keycode+offset+r)%50);
                    if(idle_brightness==0) {
                        kdisp_enable(false);
                    } else {
                        kdisp_enable(true);
                        kdisp_set_contrast(idle_brightness-1);
                    }
                }
                sr_shift_once_latch();
            }

        }
        for (;skip > 0;skip--) {
            sr_shift_once_latch();
        }
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t* record) {

    uint32_t t = timer_elapsed32(last_update);
    if(record->event.pressed) {
        uprintf("wait %ld.%03ld\n", t/1000, t%1000);
        uprintf("press 0x%04x\n", keycode);
    } else {
        uprintf("wait %ld.%03ld\n", t/1000, t%1000);
        uprintf("release 0x%04x\n", keycode);
    }

     if(process_unicodemap_poly(keycode, record)) {
        return  false;
    }

    switch (keycode) {
        case KC_CRSEL:
            if (record->event.pressed) { SEND_STRING(SS_TAP(X_HOME) SS_TAP(X_HOME) SS_LSFT(SS_TAP(X_END)) SS_TAP(X_BACKSPACE) SS_TAP(X_BACKSPACE) SS_TAP(X_DOWN)); }
            uprint("Delete Line.\n");
            return false;
        case KC_SELECT:
            if (record->event.pressed) { SEND_STRING(SS_LCTL(SS_TAP(X_LEFT) SS_LSFT(SS_TAP(X_RGHT)))); }
            uprint("Select Word.\n");
            return false;
        case KC_EXSEL:
            if (record->event.pressed) { SEND_STRING(SS_TAP(X_HOME) SS_LSFT(SS_TAP(X_END))); }
            uprint("Select Line.\n");
            return false;
        case KC_OPER:
            if (record->event.pressed) {
                SEND_STRING( // Go to the end of the line and tap delete.
                    SS_TAP(X_END) SS_TAP(X_DEL)
                    SS_TAP(X_SPC) // In case this has joined two wormatrix toaend_string splhhhhds together, insert one space.
                    SS_LCTL(
                        // Go to the beginning of the next word.
                        SS_TAP(X_RGHT) SS_TAP(X_LEFT)
                        // Select back to the end of the previous word. This should select
                        // all spaces and tabs between the joined lines from indentation
                        // or trailing whitespace, including the space inserted earlier.
                        SS_LSFT(SS_TAP(X_LEFT) SS_TAP(X_RGHT))
                    )
                    SS_TAP(X_SPC) // Replace the selection with a single space.
                );
                uprint("Join Line.\n");
            }
            return false;
        default:
            break;
    }
    const bool addlang = get_highest_layer(l_layer.layer)==_ADDLANG1;
    if (record->event.pressed) {
        switch (keycode) {
            case QK_BOOTLOADER:
                uprintf("Bootloader entered. Please copy new Firmware.\n");
                clear_all_displays();
                display_message(1, 1, u"BOOT-", &FreeSansBold24pt7b);
                display_message(3, 0, u"LOADER!", &FreeSansBold24pt7b);
                return true;
            case KC_A ... KC_Z:
                l_last.latin_kc = keycode;
                if((get_mods() & MOD_MASK_ALT) == 0 && addlang) {
                    const bool lshift = get_mods() == MOD_BIT(KC_LEFT_SHIFT);
                    const bool rshift = get_mods() == MOD_BIT(KC_RIGHT_SHIFT);
                    const bool upper_case = lshift || rshift || g_layer.led_state.caps_lock;
                    const uint8_t offset = upper_case ? 0 : 26;
                    if(latin_ex_map[offset+keycode-KC_A][0]) {
                        uint8_t variation = upper_case ? g_latin.ex[keycode-KC_A]>>4 : g_latin.ex[keycode-KC_A]&0xf;

                        //this is a work-around (at least for I-Bus on Linux we need to remove the shift, otherwise the Unicode sequence will not be recognized!)
                        if(lshift) unregister_code16(KC_LEFT_SHIFT);
                        if(rshift) unregister_code16(KC_RIGHT_SHIFT);
                        register_unicode(latin_ex_map[offset+keycode-KC_A][variation][0]);
                        if(lshift) register_code16(KC_LEFT_SHIFT);
                        if(rshift) register_code16(KC_RIGHT_SHIFT);
                        return false;
                    }
                }
                break;
            default:
                break;
        }

        if((get_mods() & MOD_MASK_ALT) != 0 && addlang) {
            switch(keycode) {
                case KC_LAT0 ... KC_LAT9:
                    if( l_last.latin_kc!=0) {
                        uint8_t current = g_latin.ex[l_last.latin_kc-KC_A];
                        if((get_mods() & MOD_MASK_SHIFT) || g_layer.led_state.caps_lock) {
                            g_latin.ex[l_last.latin_kc-KC_A] = ((keycode-KC_LAT0)<<4) | (current&0xf);
                        } else {
                            g_latin.ex[l_last.latin_kc-KC_A] = (keycode-KC_LAT0) | (current&0xf0);
                        }
                        send_to_bridge(USER_SYNC_LATIN_EX_DATA, (void*)&g_latin, sizeof(g_latin), 10);

                        save_user_eeconf();
                        request_disp_refresh();
                    }
                    break;
                case KC_A ... KC_Z:
                    request_disp_refresh();
                    break;
                default:
                    break;
            }

            return false;
        } else {
            l_last.latin_kc = 0;
        }
    }

    return display_wakeup(record);
}

void post_process_record_user(uint16_t keycode, keyrecord_t* record) {
    if (keycode == KC_CAPS_LOCK) {
        request_disp_refresh();
    }

    if (!record->event.pressed) {
        switch (keycode) {
        case KC_RGB_TOG:
            l_state.flags = toggle_flag(l_state.flags, RGB_ON);
            break;
        case KC_DEADKEY:
            l_state.flags = toggle_flag(l_state.flags, DEAD_KEY_ON_WAKEUP);
            request_disp_refresh();
            break;
        case KC_TOGMODS:
            l_state.flags = toggle_flag(l_state.flags, MODS_AS_TEXT);
            request_disp_refresh();
            break;
        case KC_TOGTEXT:
            l_state.flags = toggle_flag(l_state.flags, MORE_TEXT);
            request_disp_refresh();
            break;
        case KC_L0:
            l_layer.def_layer = _L0;
            persistent_default_layer_set(l_layer.def_layer);
            request_disp_refresh();
            break;
        case KC_L1:
            l_layer.def_layer = _L1;
            persistent_default_layer_set(l_layer.def_layer);
            request_disp_refresh();
            break;
        case KC_L2:
            l_layer.def_layer = _L2;
            persistent_default_layer_set(l_layer.def_layer);
            request_disp_refresh();
            break;
        case KC_L3:
            l_layer.def_layer = _L3;
            persistent_default_layer_set(l_layer.def_layer);
            request_disp_refresh();
            break;
        case KC_L4:
            l_layer.def_layer = _L4;
            persistent_default_layer_set(l_layer.def_layer);
            request_disp_refresh();
            break;
        case KC_BASE:
            layer_clear();
            layer_on(l_layer.def_layer);
            break;
        case KC_RIGHT_SHIFT:
        case KC_LEFT_SHIFT:
            request_disp_refresh();
            break;
        case KC_D1Q:
            l_state.contrast = FULL_BRIGHT/4;
            save_user_eeconf();
            break;
        case KC_D3Q:
            l_state.contrast = (FULL_BRIGHT/4)*3;
            save_user_eeconf();
            break;
        case KC_DHLF:
            l_state.contrast = FULL_BRIGHT/2;
            save_user_eeconf();
            break;
        case KC_DMAX:
            l_state.contrast = FULL_BRIGHT;
            save_user_eeconf();
            break;
        case KC_DMIN:
            l_state.contrast = 2;
            save_user_eeconf();
            break;
        case KC_DDIM:
            dec_brightness();
            break;
        case KC_DBRI:
            inc_brightness();
            break;
        /*[[[cog
            for lang in languages:
                cog.outl(f'case KC_{lang}: l_state.lang = {lang}; save_user_eeconf(); layer_off(_LL); break;')
            ]]]*/
        case KC_LANG_EN: l_state.lang = LANG_EN; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_DE: l_state.lang = LANG_DE; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_FR: l_state.lang = LANG_FR; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_ES: l_state.lang = LANG_ES; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_PT: l_state.lang = LANG_PT; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_IT: l_state.lang = LANG_IT; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_TR: l_state.lang = LANG_TR; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_KO: l_state.lang = LANG_KO; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_JA: l_state.lang = LANG_JA; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_AR: l_state.lang = LANG_AR; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_GR: l_state.lang = LANG_GR; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_UA: l_state.lang = LANG_UA; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_RU: l_state.lang = LANG_RU; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_BE: l_state.lang = LANG_BE; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_KZ: l_state.lang = LANG_KZ; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_BG: l_state.lang = LANG_BG; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_PL: l_state.lang = LANG_PL; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_RO: l_state.lang = LANG_RO; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_ZH: l_state.lang = LANG_ZH; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_NL: l_state.lang = LANG_NL; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_HE: l_state.lang = LANG_HE; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_SV: l_state.lang = LANG_SV; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_FI: l_state.lang = LANG_FI; save_user_eeconf(); layer_off(_LL); break;
        case KC_LANG_NO: l_state.lang = LANG_NO; save_user_eeconf(); layer_off(_LL); break;
        //[[[end]]]
        case KC_F1:case KC_F2:case KC_F3:case KC_F4:case KC_F5:case KC_F6:
        case KC_F7:case KC_F8:case KC_F9:case KC_F10:case KC_F11:case KC_F12:
            layer_off(_LL);
            break;
        default:
            break;
        }
    }
    else {
        switch (keycode)
        {
        case KC_RIGHT_SHIFT:
        case KC_LEFT_SHIFT:
            request_disp_refresh();
            break;
        case KC_LANG:
            if (IS_LAYER_ON(_LL)) {
                l_state.lang = (l_state.lang + 1) % NUM_LANG;
                save_user_eeconf();
                layer_off(_LL);
            }
            else {
                layer_on(_LL);
            }
            break;
        case RGB_MOD:
        case RGB_RMOD:
            request_disp_refresh();
            break;
        default:
            break;
        }
    }

    // uprintf("Key 0x%04X, col/row: %u/%u, %s, time: %u, int: %d, cnt: %u\n",
    //     keycode, record->event.key.col, record->event.key.row, record->event.pressed ? "DN" : "UP",
    //     record->event.time, record->tap.interrupted ? 1 : 0, record->tap.count);

    update_performed();
};

void show_splash_screen(void) {
    clear_all_displays();
    display_message(1, 1, u"POLY", &FreeSansBold24pt7b);
    display_message(2, 1, u"KYBD", &FreeSansBold24pt7b);
    wait_ms(400);
    clear_all_displays();
    display_message(1, 1, u"SPLIT", &FreeSansBold24pt7b);
    display_message(3, 1, u" 7 2", &FreeSansBold24pt7b);
    //wait_ms(100);
}

void set_displays(uint8_t contrast, bool idle) {
    if(idle) {
        kdisp_idle(contrast);
    } else {
        select_all_displays();
        if(contrast==DISP_OFF) {
            kdisp_enable(false);
        } else {
            kdisp_enable(true);
            kdisp_set_contrast(contrast - 1);
        }
    }
}



//disable first keypress if the displays are turned off
bool display_wakeup(keyrecord_t* record) {
    bool accept_keypress = true;
    if ((l_state.contrast==DISP_OFF || (l_state.flags & DISP_IDLE)!=0) && record->event.pressed) {
        if(l_state.contrast==DISP_OFF && (l_state.flags&DEAD_KEY_ON_WAKEUP)!=0) {
            accept_keypress = timer_elapsed32(last_update) <= TURN_OFF_TIME;
        }
        poly_eeconf_t ee = load_user_eeconf();
        l_state.contrast = ee.brightness;
        l_state.flags &= ~((uint8_t)DISP_IDLE);
        l_state.flags |= STATUS_DISP_ON;
        update_performed();
        request_disp_refresh();
    }

    return accept_keypress;
}

void unicode_input_mode_set_user(uint8_t unicode_mode) {
    l_state.unicode_mode = unicode_mode;
    request_disp_refresh();
}

void keyboard_post_init_user(void) {
    // Customise these values to desired behaviour
    debug_enable = true;
    debug_matrix = false;
    debug_keyboard = false;
    debug_mouse = false;

    //pointing_device_set_cpi(20000);
    pointing_device_set_cpi(650);
    //pimoroni_trackball_set_rgbw(0,0,255,100);
    l_layer.def_layer = persistent_default_layer_get();
    l_state.unicode_mode = get_unicode_input_mode();
    layer_clear();
    layer_on(l_layer.def_layer);

    //set these values, they will never change
    com = is_keyboard_master() ? USB_HOST : BRIDGE;
    set_side(is_keyboard_left() ? LEFT_SIDE : RIGHT_SIDE);


    //encoder pins
    setPinInputHigh(GP25);
    setPinInputHigh(GP29);

    //srand(halGetCounterValue());

    memset(&g_state, 0, sizeof(g_state));
    memset(&use_overlay, 0, sizeof(use_overlay));

    transaction_register_rpc(USER_SYNC_POLY_DATA,       user_sync_poly_data_handler);
    transaction_register_rpc(USER_SYNC_LAYER_DATA,      user_sync_layer_data_handler);
    transaction_register_rpc(USER_SYNC_LASTKEY_DATA,    user_sync_lastkey_data_handler);
    transaction_register_rpc(USER_SYNC_LATIN_EX_DATA,   user_sync_latin_ex_data_handler);
    transaction_register_rpc(USER_SYNC_OVERLAY_DATA,    user_sync_overlay_data_handler);

    #ifdef VIA_ENABLE
        transaction_register_rpc(USER_SYNC_VIA_DATA,    user_sync_via_data_handler);
    #endif

}

void keyboard_pre_init_user(void) {
    kdisp_hw_setup();
    kdisp_init(NUM_SHIFT_REGISTERS);
    peripherals_reset();
    kdisp_setup(true);

    select_all_displays();
    kdisp_scroll_vlines(47);
    kdisp_scroll_modehv(true, 3, 1);
    kdisp_scroll(false);

    poly_eeconf_t ee = load_user_eeconf();

    memset(&l_layer, 0, sizeof(l_layer));
    memset(&g_layer, 0, sizeof(g_layer));
    memset(&l_state, 0, sizeof(l_state));
    memset(&g_state, 0, sizeof(g_state));

    l_state.lang = ee.lang;
    l_state.contrast = ee.brightness;
    l_state.flags = set_flag(STATUS_DISP_ON, RGB_ON, rgb_matrix_is_enabled());

    memcpy(g_latin.ex, ee.latin_ex, sizeof(g_latin.ex));

    set_displays(l_state.contrast, false);
    l_last.latin_kc = 0;
    show_splash_screen();

    setPinInputHigh(I2C1_SDA_PIN);
}

void eeconfig_init_user(void) {
    uprint("Init EE config\n");
    poly_eeconf_t ee;
    ee.lang = g_lang_init;
    ee.brightness = ~FULL_BRIGHT;
    ee.unused = 0;
    memset(ee.latin_ex, 0, sizeof(ee.latin_ex));
    eeconfig_read_user_datablock(&ee);
}


void num_to_u16_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    if(value<10) {
        snprintf((char*) buffer, buffer_len, "%d", value);
        buffer[1] = 0;
        buffer[2] = 0;
        buffer[3] = 0;
    } else if(value>99) {
        snprintf((char*) buffer, buffer_len, "%d %d %d", value/100, (value/10)%10, value%10);
        buffer[1] = 0;
        buffer[3] = 0;
        buffer[5] = 0;
        buffer[6] = 0;
        buffer[7] = 0;
    } else {
        snprintf((char*) buffer, buffer_len, "%d %d", value/10, value%10);
        buffer[1] = 0;
        buffer[3] = 0;
        buffer[4] = 0;
        buffer[5] = 0;
    }
}

void hex_to_u16_string(char* buffer, uint8_t buffer_len, uint8_t value) {
    if(value<16) {
        snprintf((char*) buffer, buffer_len, "%X", value);
        buffer[1] = 0;
        buffer[2] = 0;
        buffer[3] = 0;
    } else {
        snprintf((char*) buffer, buffer_len, "%X %X", value/16, value%16);
        buffer[1] = 0;
        buffer[3] = 0;
        buffer[4] = 0;
        buffer[5] = 0;
    }
}

void oled_update_buffer(void) {
    uint16_t buffer[32];

    kdisp_set_buffer(0);

    const GFXfont* displayFont[] = { &NotoSans_Regular11pt7b };
    const GFXfont* smallFont[] = { &NotoSans_Medium8pt7b };
    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 0, 14, ICON_LAYER);
    hex_to_u16_string((char*) buffer, sizeof(buffer), get_highest_layer(g_layer.layer));
    kdisp_write_gfx_text(displayFont, 1, 20, 14, buffer);
    if(side_is_undecided()) {
        kdisp_write_gfx_text(displayFont, 1, 50, 14, u"Uknw");
    } else {
        kdisp_write_gfx_text(displayFont, 1, 38, 14, is_left_side() ? u"LEFT" : u"RIGHT");
    }

    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 108, 16, g_layer.led_state.num_lock ? ICON_NUMLOCK_ON : ICON_NUMLOCK_OFF);
    kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 108, 38, g_layer.led_state.caps_lock ? ICON_CAPSLOCK_ON : ICON_CAPSLOCK_OFF);
    if(g_layer.led_state.scroll_lock) {
        kdisp_write_gfx_text(ALL_FONTS, ALL_FONT_SIZE, 112, 54, ARROWS_DOWNSTOP);
    } else {
        kdisp_write_gfx_text(smallFont, 1, 112, 56, is_usb_host_side() ? u"H" : u"B");
    }

    if(is_right_side()) {
        kdisp_write_gfx_text(smallFont, 1, 0, 30, u"RGB");

        if(!rgb_matrix_is_enabled()) {
            kdisp_write_gfx_text(smallFont, 1, 34, 30, u"Off");
        } else {
            num_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_mode());
            kdisp_write_gfx_text(smallFont, 1, 34, 30, buffer);
            kdisp_write_gfx_text(smallFont, 1, 58, 30, get_led_matrix_text(rgb_matrix_get_mode()));
            kdisp_write_gfx_text(smallFont, 1, 0, 44, u"HSV");
            hex_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_hue());
            kdisp_write_gfx_text(smallFont, 1, 38, 44, buffer);
            hex_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_sat());
            kdisp_write_gfx_text(smallFont, 1, 60, 44, buffer);
            hex_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_val());
            kdisp_write_gfx_text(smallFont, 1, 82, 44, buffer);
            kdisp_write_gfx_text(smallFont, 1, 0, 58, u"Speed");
            num_to_u16_string((char*) buffer, sizeof(buffer), rgb_matrix_get_speed());
            kdisp_write_gfx_text(smallFont, 1, 58, 58, buffer);
        }
    } else {
        switch(l_layer.def_layer) {
            case 0: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Qwerty"); break;
            case 1: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Qwerty Stag!"); break;
            case 2: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Colemak DH"); break;
            case 3: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Neo"); break;
            case 4: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Workman"); break;
            default: kdisp_write_gfx_text(smallFont, 1, 0, 30, u"Unknown"); break;
        }
        kdisp_write_gfx_text(smallFont, 1, 0, 44, u"Dsp*");
        num_to_u16_string((char*) buffer, sizeof(buffer), l_state.contrast);
        kdisp_write_gfx_text(smallFont, 1, 42, 44, buffer);
        uint8_t i=0;
        for(;i<(l_state.contrast/5);++i) {
            buffer[i] = 'l';
        }
        buffer[i] = 0;
        buffer[i+1] = 0;
        kdisp_write_gfx_text(smallFont, 1, 64, 44, buffer);

        kdisp_write_gfx_text(smallFont, 1, 0, 58, u"WPM");
        num_to_u16_string((char*) buffer, sizeof(buffer), get_current_wpm());
        kdisp_write_gfx_text(smallFont, 1, 44, 58, buffer);

        kdisp_write_gfx_text(smallFont, 1, 68, 58, u"L");
        num_to_u16_string((char*) buffer, sizeof(buffer), l_state.lang);
        kdisp_write_gfx_text(smallFont, 1, 84, 58, buffer);
    }
}

void oled_status_screen(void) {
     if( (l_state.flags&STATUS_DISP_ON) == 0) {
        oled_off();
        return;
    } else if( (l_state.flags&STATUS_DISP_ON) != 0) {
        oled_on();
    }

    oled_update_buffer();
    oled_clear();
    oled_write_raw((char *)get_scratch_buffer(), get_scratch_buffer_size());
}

void oled_render_logos(void) {
    if(is_left_side()) {
        oled_draw_poly();
        oled_scroll_right();
    } else {
        oled_draw_kybd();
        oled_scroll_left();
    }
}

bool oled_task_user(void) {
    if((l_state.flags&DISP_IDLE) != 0) {
        oled_render_logos();
    } else {
        oled_scroll_off();
        oled_status_screen();
    }
    return false;
}

const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [1] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [2] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [3] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [4] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [5] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [6] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [7] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [8] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [9] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [10] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [11] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [12] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
    [13] =  { ENCODER_CCW_CW(KC_WH_D, KC_WH_U)},
};



oled_rotation_t oled_init_user(oled_rotation_t rotation){
    oled_off();
    oled_clear();
    oled_render();
    oled_scroll_set_speed(0);
    oled_render_logos();
    oled_on();
    return rotation;
}

void poly_suspend(void) {
    l_state.overlay_flags = flag_off(l_state.overlay_flags, DISPLAY_OVERLAYS);
    l_state.flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE);// & ~((uint8_t)RGB_ON);
    l_state.contrast = DISP_OFF;
}

void suspend_power_down_kb(void) {
    poly_suspend();
    rgb_matrix_disable_noeeprom();
    housekeeping_task_user();
    suspend_power_down_user();
    last_update = -1;
}


void suspend_wakeup_init_kb(void) {
    l_state.flags |= STATUS_DISP_ON;
    l_state.flags &= ~((uint8_t)DISP_IDLE);
    poly_eeconf_t ee = load_user_eeconf();
    l_state.contrast = ee.brightness;
    last_update = 0;

    //rgb_matrix_reload_from_eeprom();
    if(test_flag(l_state.flags, RGB_ON)) {
        rgb_matrix_enable_noeeprom();
    }

    update_performed();
    housekeeping_task_user();
    suspend_wakeup_init_user();
}



static uint16_t byte_counter = 0;
void fill_overlay_buffer(uint8_t keycode, uint8_t mods, uint8_t segment_0_to_14, uint8_t* buffer_24bytes) {
    if (keycode > KC_RGUI) {
        uprint("Warning: Supplied overlay keycode not supported.\n");
        return;
    }
    uint16_t idx = (keycode > KC_APP) ? (keycode - KC_LEFT_CTRL + 82) : (keycode > KC_NUM_LOCK ? keycode - KC_NUBS + 80 : keycode - KC_A);
    if (idx >= 90) {
        uprint("Warning: Calculated index for overlay out of bounds. Dropping overlay.\n");
        return;
    }
    idx = adjust_overlay_idx_to_mod(idx, mods);

    enum key_split_pos pos = get_split_matrix_side(keycode, l_layer.def_layer);
    //bool current = is_on_current_split_matrix_side(keycode, get_highest_layer(l_layer.def_layer));
    if (is_on_current_side(pos)) {
        memcpy(overlays[idx] + segment_0_to_14 * BYTES_PER_SEGMENT, buffer_24bytes, BYTES_PER_SEGMENT);
    }
    //only send to bridge when needed
    if (is_on_other_side(pos)) {
        overlay_sync_t transfer;
        transfer.segment = segment_0_to_14;
        transfer.adj_idx = idx;
        memcpy(&transfer.overlay, buffer_24bytes, BYTES_PER_SEGMENT);
        send_to_bridge(USER_SYNC_OVERLAY_DATA, (void*)&transfer, sizeof(transfer), 10);
    }

    byte_counter += BYTES_PER_SEGMENT;
    if (segment_0_to_14 == NUM_SEGMENTS_PER_OVERLAY - 1) {
        use_overlay[idx] = true;
        uprintf("Received overlay for keycode 0x%x (modifiers: 0x%x): %d bytes, index %d, side: %s.\n", keycode, mods, byte_counter, idx, pos_to_str(pos));
        byte_counter = 0;
    }
}

void switch_events_poly(uint8_t row, uint8_t col, bool pressed) {
#if defined(LED_MATRIX_ENABLE)
    led_matrix_handle_key_event(row, col, pressed);
#endif
#if defined(RGB_MATRIX_ENABLE)
    rgb_matrix_handle_key_event(row, col, pressed);
#endif
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    const char * name = "P\x06.Split72 " FW_VERSION " HW" STR(DEVICE_VER) " ";

    if(length>1 && (data[0] == /*via_command_id::*/id_custom_save || data[0] == 'P')) {
        switch(data[1]) {
            case id_custom_channel...id_qmk_led_matrix_channel: //unusable :(
                break;
            case 6: //id
                memset(data, 0, length);
                memcpy(data, name, strlen(name));
                break;
            case 7: //lang
                memset(data, 0, length);
                switch(l_state.lang) {
                    /*[[[cog
                    for lang in languages:
                        cog.outl(f'case {lang}: memcpy(data, "P\\x07.{lang[5:]}", 5); break;')
                    ]]]*/
                    case LANG_EN: memcpy(data, "P\x07.EN", 5); break;
                    case LANG_DE: memcpy(data, "P\x07.DE", 5); break;
                    case LANG_FR: memcpy(data, "P\x07.FR", 5); break;
                    case LANG_ES: memcpy(data, "P\x07.ES", 5); break;
                    case LANG_PT: memcpy(data, "P\x07.PT", 5); break;
                    case LANG_IT: memcpy(data, "P\x07.IT", 5); break;
                    case LANG_TR: memcpy(data, "P\x07.TR", 5); break;
                    case LANG_KO: memcpy(data, "P\x07.KO", 5); break;
                    case LANG_JA: memcpy(data, "P\x07.JA", 5); break;
                    case LANG_AR: memcpy(data, "P\x07.AR", 5); break;
                    case LANG_GR: memcpy(data, "P\x07.GR", 5); break;
                    case LANG_UA: memcpy(data, "P\x07.UA", 5); break;
                    case LANG_RU: memcpy(data, "P\x07.RU", 5); break;
                    case LANG_BE: memcpy(data, "P\x07.BE", 5); break;
                    case LANG_KZ: memcpy(data, "P\x07.KZ", 5); break;
                    case LANG_BG: memcpy(data, "P\x07.BG", 5); break;
                    case LANG_PL: memcpy(data, "P\x07.PL", 5); break;
                    case LANG_RO: memcpy(data, "P\x07.RO", 5); break;
                    case LANG_ZH: memcpy(data, "P\x07.ZH", 5); break;
                    case LANG_NL: memcpy(data, "P\x07.NL", 5); break;
                    case LANG_HE: memcpy(data, "P\x07.HE", 5); break;
                    case LANG_SV: memcpy(data, "P\x07.SV", 5); break;
                    case LANG_FI: memcpy(data, "P\x07.FI", 5); break;
                    case LANG_NO: memcpy(data, "P\x07.NO", 5); break;
                    //[[[end]]]
                    default:
                        memcpy(data, "P\x07!", 3);
                        break;
                }
                break;
            case 8: //lang list
                memset(data, 0, length);
                /*[[[cog
                lang_list = "P\\x08."
                for lang in languages:
                    lang_list += lang[5:]
                    if len(lang_list)>=(32-1):
                        cog.outl(f'memcpy(data, "{lang_list}", {len(lang_list)-3});')
                        cog.outl(f'raw_hid_send(data, length);')
                        cog.outl(f'memset(data, 0, length);')
                        lang_list = "P\\x08."
                    elif lang != languages[-1]:
                        lang_list += ","
                cog.outl(f'memcpy(data, "{lang_list}", {len(lang_list)-3});')
                ]]]*/
                memcpy(data, "P\x08.EN,DE,FR,ES,PT,IT,TR,KO,JA", 29);
                raw_hid_send(data, length);
                memset(data, 0, length);
                memcpy(data, "P\x08.AR,GR,UA,RU,BE,KZ,BG,PL,RO", 29);
                raw_hid_send(data, length);
                memset(data, 0, length);
                memcpy(data, "P\x08.ZH,NL,HE,SV,FI,NO", 20);
                //[[[end]]]
                break;
            case 9: //change language
                if(data[3]< NUM_LANG) {
                    l_state.lang = data[3];
                    uprintf("Setting lang to %u.\n", data[3]);
                    request_disp_refresh();
                    update_performed();
                    memcpy(data, "P\x09.", 3);
                } else {
                    uprintf("Invalid language index %u.\n", data[3]);
                    memset(data, 0, length);
                    memcpy(data, "P\x09!", 3);
                }
                break;
            case 10: //receive overlay
                {
                    uint8_t keycode = data[3];
                    uint8_t segment = data[5];
                    if(keycode>=KC_A && keycode<=KC_RIGHT_GUI && segment<NUM_SEGMENTS_PER_OVERLAY) {
                        uint8_t mods = data[4];
                        if((mods & MOD_MASK_GUI)==0) { //for now we filter out the gui key
                            fill_overlay_buffer(keycode, mods,segment, &data[6]);
                            if(segment==NUM_SEGMENTS_PER_OVERLAY-1) {
                                update_performed();
                                request_disp_refresh();
                            }
                        }
                        memcpy(data, "P\x0a.", 3);
                    } else {
                        memset(data, 0, length);
                        memcpy(data, "P\x0a!", 3);
                    }
                }
                break;
            case 11: //overlays flags on
                {
                    const bool vis_changed = test_flag(data[3]^l_state.overlay_flags, DISPLAY_OVERLAYS);
                    l_state.overlay_flags = flag_on(l_state.overlay_flags, data[3]);
                    if(vis_changed) {
                        housekeeping_task_user();
                    }
                    memcpy(data, "P\x0b.", 3);
                    uprintf("Overlay flags 0x%x set.\n", data[3]);
                }
                break;

            case 12: //overlays flags off
                {
                    const bool vis_changed = test_flag(data[3]^l_state.overlay_flags, DISPLAY_OVERLAYS);
                    l_state.overlay_flags = flag_off(l_state.overlay_flags, data[3]);
                    if(vis_changed) {
                        housekeeping_task_user();
                    }
                    memcpy(data, "P\x0c.", 3);
                    uprintf("Overlay flags 0x%x cleared.\n", data[3]);
                }
                break;
            case 13: //set brightness
                if ( data[3] <= FULL_BRIGHT) {
                    l_state.contrast = data[3];
                    save_user_eeconf();
                    memcpy(data, "P\x0d.", 3);
                    uprintf("Set brightness to: %u.\n", data[3]);
                } else {
                    memcpy(data, "P\x0d!", 3);
                    uprintf("Refused to set brightness to: %u.\n", data[3]);
                }
                break;
            case 14: //key press
                {
                    uint16_t keycode = ((uint16_t)data[3])<<8 | data[4];
                    uint8_t r, c;
                    enum key_split_pos pos = get_split_matrix_pos(keycode, get_highest_layer(l_layer.layer), &r, &c, is_left_side());
                    const bool pressed = data[5] == 0;
                    if(pos==POS_NOT_FOUND) {
                        //actually it should be the previous layer instead of default, but it worked so far
                        pos = get_split_matrix_pos(keycode, l_layer.def_layer, &r, &c, is_left_side());
                    }
                    if (is_on_current_side(pos)) {
                        invert_display(r, c, pressed);
                    }
                    //a key can be on both sides, so no else here
                    if (is_on_other_side(pos)) {
                        // send to bridge
                        const uint8_t data_len = 6;
                        via_sync_t sync_data;
                        memcpy(&sync_data.via_commands, data, data_len);
                        send_to_bridge(USER_SYNC_VIA_DATA, (void*)&sync_data, sizeof(sync_data.crc32)+data_len, 3);
                    }
                    action_exec(MAKE_KEYEVENT(r, c, pressed));
                    switch_events_poly(r,c, pressed);

                    memcpy(data, "P\x0e.", 3);
                }
                break;
            case 15: //start/stop idle
                if(data[3]==0) {
                    if((l_state.flags & (STATUS_DISP_ON|DISP_IDLE))==0) {
                        suspend_wakeup_init_kb();
                    } else {
                        l_state.flags &= ~((uint8_t)DISP_IDLE);
                        request_disp_refresh();
                        update_performed();
                    }
                    uprint("Stop idle.\n");
                } else {
                    last_update = timer_read32() - FADE_OUT_TIME;
                    if(last_update<0) {
                        uprintf("Starting idle in %ld msec .\n", -last_update);
                        last_update=0;
                    } else {
                        uprint("Start idle.\n");
                    }
                }
                memcpy(data, "P\x0f.", 3);
                break;
            default:
                printf("Unknown command: %u.\n", data[1]);
                break;
        }
        #ifndef VIA_ENABLE
            raw_hid_send(data, length);
        #endif
    }
}

#ifndef VIA_ENABLE

void raw_hid_receive(uint8_t *data, uint8_t length) {
    via_custom_value_command_kb(data, length);
}

#else

bool via_command_kb(uint8_t *data, uint8_t length) {
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
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_set_buffer_poly(offset, size, &command_data[3]);
            data_len = 32;
            break;
        }
        default:
            return false;
    }
    via_sync_t sync_data;
    memcpy(&sync_data.via_commands, data, data_len);
    send_to_bridge(USER_SYNC_VIA_DATA, (void*)&sync_data, sizeof(sync_data.crc32)+data_len, 10);
    request_disp_refresh();
    raw_hid_send(data, length);
    return true;
}

#endif
