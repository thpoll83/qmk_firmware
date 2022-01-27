#include QMK_KEYBOARD_H
#include "print.h"
#include "kdisp.h"
#include "fonts/FreeSans12pt7b.h"

enum kb_layers { _LAYER0 = 0, _LAYER1 = 1, _LAYER2 = 2, _LAYER3 = 3, _LAYER4 = 4, _LAYER5 = 5, _LAYER6 = 6, _LAYER7 = 7, _LAYER8 = 8, _LAYER9 = 9, NUM_LAYERS = 10 };
uint16_t currentLayer = 0;

// enum custom_keycodes { KC_LAYER_1 = SAFE_RANGE, KC_LAYER_2, KC_LAYER_3 };

uint8_t g_rgb_matrix_mode = RGB_MATRIX_SOLID_REACTIVE_NEXUS;

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_LAYER0] = LAYOUT(KC_1,    KC_2,   KC_3,      KC_4),
    [_LAYER1] = LAYOUT(KC_5,    KC_6,   KC_7,      KC_8),
    [_LAYER2] = LAYOUT(KC_9,    KC_0,   KC_EQUAL,  KC_MINUS),
    [_LAYER3] = LAYOUT(KC_A,    KC_B,   KC_C,      KC_D),
    [_LAYER4] = LAYOUT(KC_E,    KC_F,   KC_G,      KC_H),
    [_LAYER5] = LAYOUT(KC_I,    KC_J,   KC_K,      KC_L),
    [_LAYER6] = LAYOUT(KC_M,    KC_N,   KC_O,      KC_P),
    [_LAYER7] = LAYOUT(KC_Q,    KC_R,   KC_S,      KC_T),
    [_LAYER8] = LAYOUT(KC_U,    KC_V,   KC_W,      KC_X),
    [_LAYER9] = LAYOUT(KC_Y,    KC_Z,   KC_SLASH,  KC_DOT),

};

led_config_t g_led_config = {{// Key Matrix to LED Index
                              {0, 1,  2, 3}},
                             {
                                 // LED Index to Physical Position
                                 {100, 32},
                                 {120, 32},
                                 {140, 32},
                                 {160, 32}
                                 //{190, 32},
                                 //{180, 32},
                                 //{170, 32},
                                 //{160, 32},
                                 //{150, 32},
                                 //{140, 32}  //, {139, 32}, {138, 32}, {137, 32}, {136, 32}, {135, 32}, {134, 32}, {133, 32}, {132, 32}, {131, 32}, {130, 32}, {129, 32}, {128, 32}, {127, 32}, {126, 32}, {125, 32}, {124, 32}, {123, 32}, {122, 32}, {121, 32}, {120, 32}, {119, 32}, {118, 32}, {117, 32}, {116, 32}, {115, 32}, {114, 32}, {113, 32}, {112, 32}, {111, 32}, {110, 32}, {109, 32}, {108, 32}, {107, 32}, {106, 32}, {105, 32}, {104, 32}, {103, 32}, {102, 32}, {101, 32}, {100, 32}, {99, 32}, {98, 32}, {97, 32}, {96, 32}, {95, 32}, {94, 32}, {93, 32}, {92, 32}, {91, 32}, {90, 32}
                             },
                             {
                                 // LED Index to Flag
                                 4, 4, 4, 4
                                 //0, 0, 2, 4, 4, 4, 4, 2, 0, 0
                                 //, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
                             }};

void process_layer_switch_user(uint16_t new_layer);

static uint32_t startup_timer;
static uint32_t timer;
static int32_t last_update;

static bool rotarySwitchPressed = false;
static bool holdShift = false;

static uint16_t last_key       = 0;
static bool     finished_timer = false;

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    timer = startup_timer = timer_read32();
    last_update = 0;
    //uprint("OLED initialized.");
    return OLED_ROTATION_180;
}

static void render_logo(void) {
    static const char PROGMEM raw_logo[] = {0x80, 0xc0, 0xc0, 0x40, 0x40, 0x40, 0xc0, 0xc0, 0xc0, 0x80, 0x00, 0x80, 0xc0, 0xc0, 0x40, 0x40, 0x40, 0x40, 0xc0, 0xc0, 0x80, 0x00, 0x80, 0xc0, 0x40, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0x80, 0x00, 0x80, 0xc0, 0x40, 0xc0, 0xc0, 0xc0, 0xc0, 0x40, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0xff, 0xff, 0x00, 0xef, 0xef, 0xef, 0xf0, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xf8, 0xf7, 0xf7, 0x07, 0xf7, 0xf8, 0xff, 0xff, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00,
                                            0x01, 0x03, 0x02, 0x03, 0x73, 0x73, 0x63, 0x03, 0x73, 0x61, 0x30, 0x01, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x01, 0x00, 0x01, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x01, 0x00, 0x01, 0x03, 0x03, 0x03, 0x03, 0x02, 0x03, 0x03, 0xff, 0xff, 0x00, 0xcf, 0xdf, 0x2f, 0xf0, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x01, 0xee, 0xee, 0xfe, 0xfe, 0xfe, 0xff, 0xff, 0x00, 0xff, 0xff, 0xf0, 0xef, 0xef, 0x0f, 0xef, 0xf0, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xee, 0xce, 0x31, 0xff, 0xff, 0x00, 0xff, 0xff, 0x01, 0xfe, 0xfe, 0xfe, 0xfe, 0x01, 0xff, 0xff, 0x00, 0xff, 0xff, 0x07, 0xb9, 0xbe, 0xbe, 0xb9, 0x07, 0xff, 0xff, 0x00, 0xff, 0xff, 0x01, 0xde, 0xde, 0x9e, 0x61, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfd, 0x03, 0xff, 0xff,
                                            0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x21, 0x27, 0x21, 0x20, 0x27, 0x22, 0x27, 0x20, 0x27, 0x23, 0x23, 0x20, 0x27, 0x25, 0x27, 0x20, 0x27, 0x27, 0x24, 0x20, 0x27, 0x27, 0x24, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x23, 0x27, 0x24, 0x27, 0x27, 0x27, 0x24, 0x27, 0x27, 0x23, 0x20, 0x23, 0x27, 0x26, 0x25, 0x25, 0x25, 0x25, 0x25, 0x27, 0x23, 0x20, 0x23, 0x27, 0x27, 0x27, 0x27, 0x24, 0x27, 0x27, 0x27, 0x23, 0x20, 0x23, 0x27, 0x24, 0x25, 0x25, 0x25, 0x25, 0x26, 0x27, 0x23, 0x20, 0x23, 0x27, 0x26, 0x25, 0x25, 0x25, 0x25, 0x26, 0x27, 0x23, 0x20, 0x23, 0x27, 0x24, 0x27, 0x27, 0x27, 0x27, 0x24, 0x27, 0x03, 0x20, 0x03, 0x27, 0x04, 0x27, 0x07, 0x67, 0x34, 0x77, 0x07, 0x13, 0x70, 0x13, 0x07, 0x74, 0x55, 0x75, 0x05, 0x76, 0x37, 0x77, 0x03};
    oled_write_raw_P(raw_logo, sizeof(raw_logo));
}

void render_info(void) {
    oled_write_P(PSTR("Layer:"), false);

    switch (get_highest_layer(layer_state)) {
        case _LAYER0:
            oled_write_ln_P(PSTR("0 "), false);
            break;
        case _LAYER1:
            oled_write_ln_P(PSTR("1 "), false);
            break;
        case _LAYER2:
            oled_write_ln_P(PSTR("2 "), false);
            break;
        case _LAYER3:
            oled_write_ln_P(PSTR("3 "), false);
            break;
        case _LAYER4:
            oled_write_ln_P(PSTR("4 "), false);
            break;
        case _LAYER5:
            oled_write_ln_P(PSTR("5 "), false);
            break;
        case _LAYER6:
            oled_write_ln_P(PSTR("6 "), false);
            break;
        case _LAYER7:
            oled_write_ln_P(PSTR("7 "), false);
            break;
        case _LAYER8:
            oled_write_ln_P(PSTR("8 "), false);
            break;
        case _LAYER9:
            oled_write_ln_P(PSTR("9 "), false);
            break;
        default:
            // Or use the write_ln shortcut over adding '\n' to the end of your string
            oled_write_ln_P(PSTR("? "), false);
    }

    // Host Keyboard Layer Status
    // Host Keyboard LED Status
    led_t led_state = host_keyboard_led_state();

    oled_write_P(led_state.num_lock ? PSTR("NUM ") : PSTR("[ ] "), false);
    oled_write_P(led_state.caps_lock ? PSTR("CAP ") : PSTR("[ ] "), false);
    oled_write_P(led_state.scroll_lock ? PSTR("SCR  ") : PSTR("[ ]  "), false);

    if(holdShift) {
        oled_write_P(PSTR("Shift\n"), false);
    } else {
        oled_write_P(PSTR("\n"), false);
    }

    char     buffer[32];
    uint32_t uptime = timer_elapsed32(startup_timer);
    snprintf(buffer, sizeof(buffer), "Mode:%2d %d:%02d v%d.%d\n", g_rgb_matrix_mode, uptime / 60000, (uptime / 1000) % 60, (char)(DEVICE_VER >> 8), (char)DEVICE_VER);
    oled_write(buffer, false);

    snprintf(buffer, sizeof(buffer), "Key: 0x%04x %s", last_key, finished_timer ? "r" : "x");
    oled_write(buffer, false);
}

// Setup some mask which can be or'd with bytes to turn off pixels
const uint8_t single_bit_masks[8] = {127, 191, 223, 239, 247, 251, 253, 254};

static void fade_display(void) {
    // Define the reader structure
    oled_buffer_reader_t reader;
    uint8_t              buff_char;
    if (random() % 30 == 0) {
        srand(timer_read());
        // Fetch a pointer for the buffer byte at index 0. The return structure
        // will have the pointer and the number of bytes remaining from this
        // index position if we want to perform a sequential read by
        // incrementing the buffer pointer
        reader = oled_read_raw(0);
        // Loop over the remaining buffer and erase pixels as we go
        for (uint16_t i = 0; i < reader.remaining_element_count; i++) {
            // Get the actual byte in the buffer by dereferencing the pointer
            buff_char = *reader.current_element;
            if (buff_char != 0) {
                oled_write_raw_byte(buff_char & single_bit_masks[rand() % 8], i);
            }
            // increment the pointer to fetch a new byte during the next loop
            reader.current_element++;
        }
    }
}

bool oled_task_user(void) {
    if (!finished_timer && (timer_elapsed32(startup_timer) < 2000)) {
        oled_set_cursor(0, 4);
        render_logo();
        //uprint("OLED: Render Logo");
    } else if (timer_elapsed32(timer)-last_update > 60000) {
        fade_display();
    } else {
        if (!finished_timer) {
            oled_clear();
            finished_timer = true;
            //uprint("OLED: Ready.");
        }
        oled_set_cursor(0, 4);
        render_logo();
        oled_set_cursor(0, 0);
        render_info();
    }

    return true;
}

// rotary encoder
void matrix_init_user(void) { setPinInputHigh(B11); }



void matrix_scan_user(void) {
    if (readPin(B11)) {
        if(rotarySwitchPressed) {
            if(holdShift) {
                register_code(KC_LSHIFT);
            } else {
                unregister_code(KC_LSHIFT);
            }
        }
        rotarySwitchPressed = false;
    } else if(!rotarySwitchPressed) {
        rotarySwitchPressed = true;
        //tap_code(KC_shift);
        holdShift = !holdShift;
        process_layer_switch_user(currentLayer);
        last_update = timer_elapsed32(timer);
    }
}

bool encoder_update_user(uint8_t index, bool clockwise) {
    if (!clockwise) {
        if(currentLayer==0) {
            currentLayer = NUM_LAYERS-1;
        } else {
        currentLayer--;
        }
    } else {
        currentLayer++;
    }
    currentLayer = currentLayer % NUM_LAYERS;

    //uprint("Activated Layer %d", currentLayer);
    process_layer_switch_user(currentLayer);
    last_update = timer_elapsed32(timer);
    return true;
}

void early_hardware_init_post(void) {
    kdisp_hw_setup();

    spi_hw_setup();
}

void keyboard_post_init_user(void) {
    rgb_matrix_set_color_all(0, 4, 4);
    rgb_matrix_mode_noeeprom(g_rgb_matrix_mode);

    // Customise these values to desired behaviour
    debug_enable   = true;
    debug_matrix   = false;
    debug_keyboard = false;
    debug_mouse    = false;

    kdisp_init();
    process_layer_switch_user(currentLayer);
}

struct disp_status {
    uint8_t bitmask;
};

struct disp_status key_display[] = {{.bitmask = ~0x01}, {.bitmask = ~0x02}, {.bitmask = ~0x04}, {.bitmask = ~0x08}};

const char* keycode_to_disp_text(uint16_t keycode) {
    bool shift = holdShift;
    switch (keycode) {
        case KC_0:
            return shift ? ")" : "0";
        case KC_1:
            return shift ? "!" : "1";
        case KC_2:
            return shift ? "@" : "2";
        case KC_3:
            return shift ? "#" : "3";
        case KC_4:
            return shift ? "$" : "4";
        case KC_5:
            return shift ? "%" : "5";
        case KC_6:
            return shift ? "^" : "6";
        case KC_7:
            return shift ? "&" : "7";
        case KC_8:
            return shift ? "*" : "8";
        case KC_9:
            return shift ? "(" : "9";
        case KC_EQUAL:
            return shift ? "+" : "=";
        case KC_MINUS:
            return shift ? "-" : "_";
        case KC_SLASH:
            return shift ? "/" : "?";
        case KC_DOT:
            return shift ? "." : ">";
        default: break;
    }
    shift |= host_keyboard_led_state().caps_lock;
    switch (keycode) {
        case KC_A:
            return shift ? "A" : "a";
        case KC_B:
            return shift ? "B" : "b";
        case KC_C:
            return shift ? "C" : "c";
        case KC_D:
            return shift ? "D" : "d";
        case KC_E:
            return shift ? "E" : "e";
        case KC_F:
            return shift ? "F" : "f";
        case KC_G:
            return shift ? "G" : "g";
        case KC_H:
            return shift ? "H" : "h";
        case KC_I:
            return shift ? "I" : "i";
        case KC_J:
            return shift ? "J" : "j";
        case KC_K:
            return shift ? "K" : "k";
        case KC_L:
            return shift ? "L" : "l";
        case KC_M:
            return shift ? "M" : "m";
        case KC_N:
            return shift ? "N" : "n";
        case KC_O:
            return shift ? "O" : "o";
        case KC_P:
            return shift ? "P" : "p";
        case KC_Q:
            return shift ? "Q" : "q";
        case KC_R:
            return shift ? "R" : "r";
        case KC_S:
            return shift ? "S" : "s";
        case KC_T:
            return shift ? "T" : "t";
        case KC_U:
            return shift ? "U" : "u";
        case KC_V:
            return shift ? "V" : "v";
        case KC_W:
            return shift ? "W" : "w";
        case KC_X:
            return shift ? "X" : "x";
        case KC_Y:
            return shift ? "Y" : "y";
        case KC_Z:
            return shift ? "Z" : "z";
        default:
            break;
    }
    return 0;
}

uint8_t matrix_disp_index(uint8_t row, uint8_t col) {
    return LAYOUT_TO_INDEX(row, col);
}

uint8_t keycode_to_disp_index(uint16_t keycode) {
   for(uint8_t row =0; row < MATRIX_ROWS; ++row) {
       for(uint8_t col =0; col < MATRIX_COLS; ++col) {
           if(keymaps[currentLayer][row][col]==keycode) {
               return matrix_disp_index(row, col);
           }
       }
   }

    return 255;
}

void process_layer_switch_user(uint16_t new_layer) {
    layer_move(new_layer);

    rgb_matrix_step_noeeprom();
    g_rgb_matrix_mode = (g_rgb_matrix_mode + 1) % RGB_MATRIX_EFFECT_MAX;

    // keypos_t key;
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            //key.col           = c;
            //key.row           = r;
            uint16_t keycode  = keymaps[new_layer][r][c];//keymap_key_to_keycode(new_layer, key);
            uint8_t  disp_idx = matrix_disp_index(r, c);

            if (disp_idx != 255) {
                const char* text = keycode_to_disp_text(keycode);
                sr_shift_out_latch(key_display[disp_idx].bitmask);
                kdisp_set_buffer(0x00);
                kdisp_write_gfx_text(&FreeSans12pt7b, 28, 18, text);
                kdisp_send_buffer();
            }
        }
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t* record) {
    uint8_t disp_idx = matrix_disp_index(record->event.key.row, record->event.key.col);
    uint8_t bitmask = key_display[disp_idx].bitmask;
    sr_shift_out_latch(bitmask);
    if (record->event.pressed) {
        last_key = keycode;
        if (disp_idx != 255) {
            //wait_ms(1);
            kdisp_invert(true);
        }
    } else {
        if (disp_idx != 255) {
            //wait_ms(1);
            kdisp_invert(false);
        }
    }

    uprintf("KL: kc: 0x%04X, col: %u, row: %u, pressed: %b, time: %u, interrupt: %b, count: %u SR bitmask: 0x%04X (%u)\n",
        keycode, record->event.key.col, record->event.key.row, record->event.pressed,
        record->event.time, record->tap.interrupted, record->tap.count, bitmask, ~bitmask);

    /*switch (keycode) {
        case KC_LAYER_1:
            if (record->event.pressed) {
                layer_move(_LAYER1);
                print("Activated Layer 1");
                process_layer_switch_user(_LAYER1);
            }
            return false;

        case KC_LAYER_2:
            if (record->event.pressed) {
                layer_move(_LAYER2);
                print("Activated Layer 2");
                process_layer_switch_user(_LAYER2);
            }
            return false;

        case KC_LAYER_3:
            if (record->event.pressed) {
                layer_move(_LAYER3);
                print("Activated Layer 3");
                process_layer_switch_user(_LAYER3);
            }
            return false;

        default:
            return true;
    }
    */

   last_update = timer_elapsed32(timer);

   return true;
};
