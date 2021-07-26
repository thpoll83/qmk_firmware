#include QMK_KEYBOARD_H
#include "print.h"
#include "spi_master.h"

enum { LAYER_1 = 0, LAYER_2 = 1, LAYER_3 = 2 };

enum { KC_LAYER_1 = SAFE_RANGE, KC_LAYER_2, KC_LAYER_3 };

uint8_t g_rgb_matrix_mode = RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS;

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [LAYER_1] = LAYOUT(KC_LAYER_2, KC_2, KC_1, KC_3),

    [LAYER_2] = LAYOUT(KC_LAYER_3, KC_5, KC_4, KC_6),

    [LAYER_3] = LAYOUT(KC_LAYER_1, KC_8, KC_7, KC_9)};

led_config_t g_led_config = {{// Key Matrix to LED Index
                              {6, 4},
                              {5, 3}},
                             {// LED Index to Physical Position
                              {220, 32},
                              {200, 32},
                              {180, 32},
                              {160, 32},
                              {140, 32},
                              {120, 32},
                              {100, 32},
                              {80, 32},
                              {60, 32},
                              {4, 32}},
                             {// LED Index to Flag
                              0, 0, 2, 4, 4, 4, 4, 2, 0, 0}};

// void suspend_power_down_kb(void)
// {
//     rgb_matrix_set_suspend_state(true);
//     suspend_power_down_user();
// }

// void suspend_wakeup_init_kb(void)
// {
//     rgb_matrix_set_suspend_state(false);
//     suspend_wakeup_init_user();
// }

uint32_t startup_timer;
uint8_t shift_status;

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    startup_timer = timer_read32();
    shift_status = 0;
    print("OLED initialized.");
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
        case LAYER_1:
            oled_write_ln_P(PSTR("1 "), false);
            break;
        case LAYER_2:
            oled_write_ln_P(PSTR("2 "), false);
            break;
        case LAYER_3:
            oled_write_ln_P(PSTR("3 "), false);
            break;
        default:
            // Or use the write_ln shortcut over adding '\n' to the end of your string
            oled_write_ln_P(PSTR("? "), false);
    }

    // Host Keyboard Layer Status
    // Host Keyboard LED Status
    led_t led_state = host_keyboard_led_state();

    oled_write_P(led_state.num_lock ? PSTR("NUM ") : PSTR("[  ] "), false);
    oled_write_P(led_state.caps_lock ? PSTR("CAP ") : PSTR("[  ] "), false);
    oled_write_P(led_state.scroll_lock ? PSTR("SCR") : PSTR("[  ]"), false);

    char buffer[32];
    uint32_t uptime = timer_elapsed32(startup_timer);
    snprintf(buffer, sizeof(buffer), "Mode:%2d %d:%02d v%d.%d", g_rgb_matrix_mode, uptime/60000, (uptime/1000)%60, (char)(DEVICE_VER >> 8), (char)DEVICE_VER);
    oled_write(buffer, false);
}


void oled_task_user(void) {
    static bool finished_timer = false;
    if (!finished_timer && (timer_elapsed32(startup_timer) < 3000)) {
        oled_set_cursor(0, 4);
        render_logo();
    } else {
        if (!finished_timer) {
            oled_clear();
            finished_timer = true;
            print("OLED timeout reached.");
        }
        oled_set_cursor(0, 4);
        render_logo();
        oled_set_cursor(0, 0);
        render_info();
    }
}

void keyboard_pre_init_user(void) {
    setPinOutput(D3);  // VDD
    setPinOutput(D1);  // VBAT

    setPinOutput(SR_NMR_PIN);
    setPinOutput(SR_CLK_PIN);
    setPinOutput(SR_DATA_PIN);
    setPinOutput(SR_LATCH_PIN);

    setPinOutput(SPI_CD_PIN);
    setPinOutput(SPI_RST_PIN);
}

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t val) {
    for (uint8_t i = 0; i < 8; i++) {
        writePinLow(clockPin);
        writePin(dataPin, !!(val & (1 << (7 - i))));
        wait_ms(1);
        writePinHigh(clockPin);

    }
}

void shiftOutLatch(int dataPin, int shiftPin, int latchPin, uint8_t myDataOut) {
    writePinLow(latchPin);
    shiftOut(dataPin, shiftPin, myDataOut);
    wait_ms(1);
    writePinHigh(latchPin);
}

void keyboard_post_init_user(void) {
    rgb_matrix_set_color_all(0, 4, 4);
    rgb_matrix_mode_noeeprom(g_rgb_matrix_mode);

    // Customise these values to desired behaviour
    debug_enable   = true;
    debug_matrix   = false;
    debug_keyboard = false;
    debug_mouse    = false;

    writePinHigh(D3);
    wait_ms(10);
    writePinHigh(D1);

    // shift register setup
    writePinHigh(SR_NMR_PIN);
    wait_ms(10);
    writePinLow(SR_NMR_PIN);
    wait_ms(10);
    writePinHigh(SR_NMR_PIN);

    shift_status = 0xaa;
    shiftOutLatch(SR_DATA_PIN, SR_CLK_PIN, SR_LATCH_PIN, shift_status);
    wait_ms(3000);
    shift_status = 0x55;
    shiftOutLatch(SR_DATA_PIN, SR_CLK_PIN, SR_LATCH_PIN, shift_status);
    wait_ms(3000);
    shift_status = 0xaa;
    shiftOutLatch(SR_DATA_PIN, SR_CLK_PIN, SR_LATCH_PIN, shift_status);

    spi_init();
    // spi_start(SPI_CS_PIN, false, 0, STM32_SYSCLK / 4000000);

    /*
    // Reset SSD1306 if requested and reset pin specified in constructor
  if (reset && (rstPin >= 0)) {
    pinMode(rstPin, OUTPUT);
    digitalWrite(rstPin, HIGH);
    delay(10);                   // VDD goes high at start, pause for 1 ms
    digitalWrite(rstPin, LOW);  // Bring reset low
    delay(100);                  // Wait 10 ms
    digitalWrite(rstPin, HIGH); // Bring out of reset
    delay(100);
  }

delay(100);

  TRANSACTION_START

  uint8_t comPins = 0x12;

  ssd1306_command1(SSD1306_DISPLAYOFF);
  static const uint8_t PROGMEM clockDiv[] = {SSD1306_SETDISPLAYCLOCKDIV, 0x80 };
  ssd1306_commandList(clockDiv, sizeof(clockDiv));
  static const uint8_t PROGMEM dispOffset[] = {SSD1306_SETDISPLAYOFFSET, 0x00};
  ssd1306_commandList(dispOffset, sizeof(dispOffset));
  ssd1306_command1(SSD1306_SETSTARTLINE | 0x0);
  ssd1306_command1(SSD1306_DISPLAYALLON_RESUME);
  ssd1306_command1(SSD1306_NORMALDISPLAY);
  static const uint8_t PROGMEM chargePump[] = {SSD1306_CHARGEPUMP, 0x95};  //0x14?
  ssd1306_commandList(chargePump, sizeof(chargePump));
  static const uint8_t PROGMEM memMode[] = {SSD1306_MEMORYMODE, 0x00};
  ssd1306_commandList(memMode, sizeof(memMode));
  ssd1306_command1(SSD1306_SEGREMAP);// | 0x1 ?
  ssd1306_command1(SSD1306_COMSCANINC); // SSD1306_COMSCANDEC ?
  static const uint8_t PROGMEM contrast[] = {SSD1306_SETCONTRAST, 0xff};
  ssd1306_commandList(contrast, sizeof(contrast));
  ssd1306_command1(SSD1306_SETPRECHARGE);
  ssd1306_command1((vccstate == SSD1306_EXTERNALVCC) ? 0x22 : 0xF1);
  static const uint8_t PROGMEM vCom[] = {SSD1306_SETVCOMDETECT, 0x20};
  ssd1306_commandList(vCom, sizeof(vCom));
  ssd1306_command1(SSD1306_SETMULTIPLEX);
  ssd1306_command1(HEIGHT - 1);
  ssd1306_command1(SSD1306_SETCOMPINS);
  ssd1306_command1(comPins);

  static const uint8_t PROGMEM fin[] = {0xad, 0x30};
  ssd1306_commandList(fin, sizeof(fin));

  ssd1306_command1(SSD1306_DISPLAYON);
    */

    print("Post init done.");
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case KC_LAYER_1:
            if (record->event.pressed) {
                layer_move(LAYER_1);
                print("Activated Layer 1");
                rgb_matrix_step_noeeprom();
                g_rgb_matrix_mode = (g_rgb_matrix_mode + 1) % RGB_MATRIX_EFFECT_MAX;
            }
            return false;

        case KC_LAYER_2:
            if (record->event.pressed) {
                layer_move(LAYER_2);
                print("Activated Layer 2");
                rgb_matrix_step_noeeprom();
                g_rgb_matrix_mode = (g_rgb_matrix_mode + 1) % RGB_MATRIX_EFFECT_MAX;
            }
            return false;

        case KC_LAYER_3:
            if (record->event.pressed) {
                layer_move(LAYER_3);
                print("Activated Layer 3");
                rgb_matrix_step_noeeprom();
                g_rgb_matrix_mode = (g_rgb_matrix_mode + 1) % RGB_MATRIX_EFFECT_MAX;
            }
            return false;

        default:
            return true;
    }
};
