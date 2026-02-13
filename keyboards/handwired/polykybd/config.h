#pragma once

#define RP2040

//#define EXTERNAL_FLASH_SIZE (8 * 1024 * 1024)

//#define UNICODE_SELECTED_MODES UNICODE_MODE_LINUX, UNICODE_MODE_MACOS, UNICODE_MODE_WINCOMPOSE
#define UNICODE_CYCLE_PERSIST true
#define UNICODE_KEY_WINC      KC_RIGHT_ALT

#define TAP_CODE_DELAY 4

#define DIODE_DIRECTION ROW2COL


// Split keyboard
// https://docs.qmk.fm/#/feature_split_keyboard?id=split-keyboard
#define SERIAL_USART_TX_PIN GP5

#define SPLIT_TRANSPORT_MIRROR
// #define SPLIT_LAYER_STATE_ENABLE
// #define SPLIT_LED_STATE_ENABLE
// #define SPLIT_MODS_ENABLE
#define SPLIT_WPM_ENABLE
#define SPLIT_TRANSACTION_IDS_USER USER_SYNC_POLY_DATA, USER_SYNC_LAYER_DATA, USER_SYNC_LASTKEY_DATA, USER_SYNC_LATIN_EX_DATA, USER_SYNC_OVERLAY_DATA, USER_SYNC_COMPRESSED_DATA, USER_SYNC_ROI_DATA, USER_SYNC_DYNAMIC_KEYMAP_DATA

#define EE_HANDS

#define I2C_DRIVER I2CD0
#define I2C1_SCL_PIN GP0
#define I2C1_SDA_PIN GP1
#define I2C1_OPMODE OPMODE_I2C
#define I2C1_CLOCK_SPEED 400000

#define OLED_INIT

#define NOP_FUDGE 0.4

//This number can be calculated by dividing the MCU’s clock speed
//by the desired SPI clock speed. For example, an MCU running at 8 MHz
//wanting to talk to an SPI device at 4 MHz would set the divisor to 2
#define SPI_DIVISOR (CPU_CLOCK / 10000000) //rp1040 runs at 133Mhz, SPI at 10Mhz


//only for v3 and later
//#define USB_VBUS_PIN GP24


// KEY_DISPLAYS_VBAT_PIN
//#define KEY_DISPLAYS_VBAT_PIN NO_PIN
//#define KEY_DISPLAYS_VDD_PIN NO_PIN

/* Reset. */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
//#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED GP17
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U

// Master to slave:
#define RPC_M2S_BUFFER_SIZE 72
// Slave to master:
#define RPC_S2M_BUFFER_SIZE 72

//######################################
//#          PolyKybd specific         #
//######################################
#define FW_VERSION "0.7.1"

#define FULL_BRIGHT 50
#define MIN_BRIGHT 1
#define DISP_OFF 0
#define BRIGHT_STEP 1

//10 sec
#define FADE_TRANSITION_TIME 10000
//2 min
#define FADE_OUT_TIME 120000
//10 min
#define TURN_OFF_TIME 1200000

//######################################
//#          Overlays specific         #
//######################################
#define HID_REPORT_SIZE RAW_EPSIZE // now 64, but might change

// segement * bytes = 360 (=2880/8 => all bits of an overlay)
#define NUM_SEGMENTS_PER_OVERLAY 6
#define BYTES_PER_SEGMENT 60

#define HID_DATA_MAX (HID_REPORT_SIZE-2) // minus via cmd byte and minus polybybd cmd byte -> -2
#define COMPRESSED_MAX HID_DATA_MAX
#define COMPRESSED_START (HID_REPORT_SIZE-4) // additional minus keycode and minus modifier -> -4

#define ROI_MAX (HID_REPORT_SIZE-2) // minus via cmd byte and minus polybybd cmd byte -> -2
#define ROI_START (HID_REPORT_SIZE-7) // additional minus keycode and 4 bytes compressed roi header -> -7

#define NUM_OVERLAYS 90
#define NUM_VARIATIONS 7 // NO_MOD(0), CTRL(1), SHIFT(2), CTRL_SHIFT(3), ALT(4), CTRL_ALT(5), ALT_SHIFT(6), Not supported without overlay mapping CTRL_ALT_SHIFT(7) GUI_KEY(8)
#define NUM_VARIATIONS_WITH_MAP 9 //all modifiers supported (current maximum would be 14, maybe later support GUI+CTL/ALT/SHIFT -> 12)
#define OVERLAY_MAP_IDX_CNT (NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP)
#define OVERLAY_MAP_IDX_BITS 10
#define OVERLAY_MAP_IDX_CNT_PER_REPORT (HID_DATA_MAX*8/OVERLAY_MAP_IDX_BITS)
#define UNSET_OVERLAY_MAPPING 0xffff

#define DEBOUNCE    5
#define PICO_FLASH_SIZE_BYTES (8 * 1024 * 1024)

#define OLED_DISPLAY_128X64
#define OLED_FONT_START	32
#define OLED_FONT_END	126
#define OLED_FONT_H "base/fonts/base_font.h"
#define OLED_BRIGHTNESS 60
#define OLED_DISABLE_TIMEOUT
#define OLED_UPDATE_INTERVAL 66 //15fps

#define MOUSEKEY_MOVE_DELTA	2

#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES
//#define RGB_MATRIX_LED_PROCESS_LIMIT 72  // limits the number of LEDs to process in an animation per task run (increases keyboard responsiveness)
#define RGB_MATRIX_LED_FLUSH_LIMIT 16    // limits in milliseconds how frequently an animation will update the LEDs. 16 (16ms) is equivalent to limiting to 60fps (increases keyboard responsiveness)
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 100
//#define RGB_MATRIX_TIMEOUT 120000
//#define RGB_DISABLE_WHEN_USB_SUSPENDED false

#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
#define ENABLE_RGB_MATRIX_SPLASH
#define ENABLE_RGB_MATRIX_MULTISPLASH
#define ENABLE_RGB_MATRIX_SOLID_SPLASH
#define ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
#define ENABLE_RGB_MATRIX_ALPHAS_MODS
#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#define ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_BREATHING
#define ENABLE_RGB_MATRIX_BAND_SAT
#define ENABLE_RGB_MATRIX_BAND_VAL
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_VAL
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_SAT
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_VAL
#define ENABLE_RGB_MATRIX_CYCLE_ALL
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_CYCLE_UP_DOWN
#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN_DUAL
#define ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
#define ENABLE_RGB_MATRIX_CYCLE_SPIRAL
#define ENABLE_RGB_MATRIX_DUAL_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_PINWHEELS
#define ENABLE_RGB_MATRIX_RAINDROPS
#define ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
#define ENABLE_RGB_MATRIX_HUE_BREATHING
#define ENABLE_RGB_MATRIX_HUE_PENDULUM
#define ENABLE_RGB_MATRIX_HUE_WAVE
#define ENABLE_RGB_MATRIX_PIXEL_FRACTAL
#define ENABLE_RGB_MATRIX_PIXEL_FLOW
#define ENABLE_RGB_MATRIX_PIXEL_RAIN

#define RGB_MATRIX_HUE_STEP 2
#define RGB_MATRIX_SAT_STEP 2
#define RGB_MATRIX_VAL_STEP 1
#define RGB_MATRIX_SPD_STEP 1

#define USE_CORE1

#define DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT 9
